#include "SessionController.h"
#include "ai/InterviewLogic.h"
#include "app/AppSettings.h"
#include <QDir>
#include <QJsonDocument>
#include <QSet>
#include <QStandardPaths>
SessionController::SessionController(QString root, QObject *p)
    : QObject(p), client(this), audio(this), remoteTranscriber(this), micTranscriber(this) {
    if (!store.open(root.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                                   : root))
        QTimer::singleShot(0, this,
                           [this] { emit status(QStringLiteral("数据库打开失败：") + store.lastError()); });
    for (auto row : store.list("sessions"))
        if (QStringList{"recording", "paused", "stopping"}.contains(row["state"].toString())) {
            row["state"] = "interrupted";
            store.put("sessions", row);
        }
    for (auto kind : QStringList{"generations", "reviews"})
        for (auto row : store.list(kind))
            if (QStringList{"Running", "Queued"}.contains(row["state"].toString())) {
                row["state"] = "Failed";
                row["error"] = QStringLiteral("上次运行中断，已保留生成内容");
                store.put(kind, row);
            }
    debounce.setSingleShot(true);
    debounce.setInterval(800);
    deadline.setSingleShot(true);
    deadline.setInterval(4000);
    connect(&debounce, &QTimer::timeout, this, [this] { extractNow(liveSid); });
    connect(&deadline, &QTimer::timeout, this, [this] { extractNow(liveSid); });
    connect(&client, &TextClient::textDelta, this, [this](QString id, QString text) {
        if (!jobs.contains(id))
            return;
        auto &job = jobs[id];
        if (job.record.value("state") == "Cancelled")
            return;
        job.record["text"] = job.record["text"].toString() + text;
        if (job.request.operation != "extract") {
            persist(job.request.operation == "review" ? "reviews" : "generations", job.record);
            emit changed(job.request.sessionId);
        }
    });
    connect(&client, &TextClient::finished, this, [this](QString id) { complete(id, true); });
    connect(&client, &TextClient::failed, this,
            [this](QString id, QString error) { complete(id, false, error); });
    connect(&audio, &AudioController::pcmReady, this, [this](QString ch, QByteArray pcm, qint64 ms) {
        (ch == "remote" ? remoteTranscriber : micTranscriber).appendAudio(pcm, ms);
    });
    connect(&audio, &AudioController::segmentSaved, this, [this](QJsonObject row) {
        if (!row.isEmpty())
            persist("audio", row);
    });
    connect(&audio, &AudioController::channelError, this, [this](QString ch, QString error) {
        if (live)
            audioIssues.append(ch + ": " + error);
        emit transcriptionStatus(ch, error);
        emit status(error);
        if (!audio.running() && live && !stoppingState)
            QTimer::singleShot(0, this, &SessionController::finish);
    });
    for (auto t : {&remoteTranscriber, &micTranscriber}) {
        connect(t, &RealtimeTranscriber::segmentChanged, this, &SessionController::acceptSegment);
        connect(t, &RealtimeTranscriber::status, this, &SessionController::transcriptionStatus);
        connect(t, &RealtimeTranscriber::gap, this, [this](QString ch, qint64 start, qint64 end) {
            persist("gaps", {{"id", newId()},
                             {"sessionId", liveSid},
                             {"channel", ch},
                             {"startMs", start},
                             {"endMs", end}});
            emit changed(liveSid);
        });
    }
}
SessionController::~SessionController() {
    audio.stop();
    remoteTranscriber.stop();
    micTranscriber.stop();
}
QJsonObject SessionController::get(QString kind, QString sid, QString id) const {
    for (auto row : store.list(kind, sid))
        if (row["id"] == id)
            return row;
    return {};
}
bool SessionController::persist(QString kind, const QJsonObject &row) {
    if (store.put(kind, row))
        return true;
    emit status(QStringLiteral("保存失败：") + store.lastError());
    if (live && !stoppingState)
        QTimer::singleShot(0, this, &SessionController::finish);
    return false;
}
QString SessionController::createSession(QString title, QString role, QString experience) {
    auto id = newId();
    if (!persist("sessions", {{"id", id},
                              {"title", title.isEmpty() ? QStringLiteral("面试记录") : title},
                              {"role", role},
                              {"experience", experience},
                              {"state", "notes"},
                              {"createdAt", utcNow()}}))
        return {};
    emit changed(id);
    return id;
}
QString SessionController::addQuestion(QString sid, QString text, bool generate) {
    if (text.trimmed().isEmpty() || get("sessions", sid, sid).isEmpty())
        return {};
    auto id = newId();
    persist("questions", {{"id", id},
                          {"sessionId", sid},
                          {"text", text.trimmed()},
                          {"sourceIds", QJsonArray()},
                          {"manual", true},
                          {"revision", 1},
                          {"createdAt", utcNow()}});
    emit changed(sid);
    if (generate)
        answerQuestion(sid, id, AppSettings().textConfig());
    return id;
}
QString SessionController::answerQuestion(QString sid, QString qid, const ProviderConfig &config) {
    auto q = get("questions", sid, qid), s = get("sessions", sid, sid);
    if (q.isEmpty() || s.isEmpty())
        return {};
    auto r = InterviewLogic::answer(s, q, store.list("transcripts", sid), config);
    QJsonObject record{{"id", r.id},
                       {"sessionId", sid},
                       {"questionId", qid},
                       {"questionRevision", q["revision"]},
                       {"provider", config.provider},
                       {"model", config.model},
                       {"state", "Queued"},
                       {"text", ""},
                       {"createdAt", utcNow()}};
    if (!persist("generations", record))
        return {};
    jobs.insert(r.id, {r, record, {}});
    answers.enqueue(r.id);
    emit changed(sid);
    pump();
    return r.id;
}
void SessionController::pump() {
    if (!activeAnswer.isEmpty())
        return;
    while (!answers.isEmpty()) {
        auto id = answers.dequeue();
        if (!jobs.contains(id))
            continue;
        auto &job = jobs[id];
        if (job.record.value("state") == "Cancelled") {
            jobs.remove(id);
            continue;
        }
        if (job.request.operation == "answer" &&
            get("questions", job.request.sessionId, job.request.questionId)["revision"] !=
                job.record["questionRevision"]) {
            job.record["state"] = "Cancelled";
            job.record["error"] = QStringLiteral("问题已修改，请重新分析");
            persist("generations", job.record);
            emit changed(job.request.sessionId);
            jobs.remove(id);
            continue;
        }
        activeAnswer = id;
        job.record["state"] = "Running";
        persist(job.request.operation == "review" ? "reviews" : "generations", job.record);
        client.start(job.request);
        return;
    }
}
QString SessionController::review(QString sid) {
    auto session = get("sessions", sid, sid);
    if (session.isEmpty())
        return {};
    if (live && sid == liveSid) {
        emit status(QStringLiteral("请先结束面试，待最终转写保存后复盘"));
        return {};
    }
    auto all = store.list("transcripts", sid);
    QList<QJsonObject> finalized;
    for (auto row : all)
        if (row["final"].toBool())
            finalized.append(row);
    if (finalized.isEmpty()) {
        emit status(QStringLiteral("没有可复盘的最终对话，请先录音转写或手动补充记录"));
        return {};
    }
    // Independent source-preserving chunks prevent long sessions from silently losing early answers.
    QList<QList<QJsonObject>> chunks;
    QList<QJsonObject> part;
    int count = 0;
    for (auto row : finalized) {
        int n = row["text"].toString().size() + 200;
        if (count + n > 10000 && !part.isEmpty()) {
            chunks.append(part);
            part.clear();
            count = 0;
        }
        part.append(row);
        count += n;
    }
    if (!part.isEmpty())
        chunks.append(part);
    QString first;
    int index = 0;
    auto config = AppSettings().textConfig();
    auto group = newId();
    for (auto chunk : chunks) {
        auto r = InterviewLogic::review(session, store.list("questions", sid), chunk, config);
        QJsonArray refs;
        for (auto row : chunk)
            refs.append(row["id"]);
        QJsonObject rec{{"id", r.id},
                        {"sessionId", sid},
                        {"provider", config.provider},
                        {"model", config.model},
                        {"state", "Queued"},
                        {"text", ""},
                        {"sourceIds", refs},
                        {"groupId", group},
                        {"part", ++index},
                        {"totalParts", chunks.size()},
                        {"createdAt", utcNow()}};
        persist("reviews", rec);
        jobs.insert(r.id, {r, rec, chunk});
        answers.enqueue(r.id);
        if (first.isEmpty())
            first = r.id;
    }
    emit changed(sid);
    pump();
    return first;
}
void SessionController::complete(QString id, bool ok, QString error) {
    if (!jobs.contains(id))
        return;
    auto job = jobs.take(id);
    auto sid = job.request.sessionId;
    if (activeAnswer == id)
        activeAnswer.clear();
    if (activeExtraction == id)
        activeExtraction.clear();
    if (job.record.value("state") != "Cancelled")
        job.record["state"] = ok ? "Completed" : "Failed";
    if (!error.isEmpty())
        job.record["error"] = error;
    if (job.request.operation == "extract" && job.record.value("state") == "Completed") {
        bool stale = false;
        for (auto source : job.sources) {
            auto current = get("transcripts", sid, source["id"].toString());
            if (current["revision"] != source["revision"])
                stale = true;
        }
        if (stale)
            emit status(QStringLiteral("提取期间转写已修订，请重新提取问题"));
        else {
            QString parseError;
            auto s = get("sessions", sid, sid);
            auto questions =
                InterviewLogic::parseQuestions(job.record["text"].toString(), s, job.sources, &parseError);
            if (!parseError.isEmpty())
                emit status(parseError);
            for (auto q : questions) {
                bool duplicate = false;
                for (auto existing : store.list("questions", sid))
                    if (existing["text"].toString().simplified() == q["text"].toString().simplified() &&
                        existing["sourceIds"] == q["sourceIds"])
                        duplicate = true;
                if (duplicate)
                    continue;
                q["provider"] = job.request.config.provider;
                q["model"] = job.request.config.model;
                persist("questions", q);
                if (autoAnswer)
                    answerQuestion(sid, q["id"].toString(), AppSettings().textConfig());
            }
        }
    } else if (job.request.operation != "extract") {
        if (ok && job.request.operation == "review") {
            auto text = job.record["text"].toString();
            bool found = false;
            for (auto source : job.sources)
                if (text.contains("[" + source["id"].toString() + "]"))
                    found = true;
            if (!found) {
                job.record["state"] = "Failed";
                job.record["error"] = QStringLiteral("报告未引用实际对话片段，证据不足，请重试");
            }
        }
        persist(job.request.operation == "review" ? "reviews" : "generations", job.record);
    }
    if (!ok && !error.isEmpty())
        emit status(error);
    emit changed(sid);
    pump();
    if (autoExtract && !pendingRemote.isEmpty() && live)
        debounce.start();
}
void SessionController::cancel(QString sid) {
    QStringList ids;
    for (auto id : jobs.keys())
        if (jobs[id].request.sessionId == sid)
            ids.append(id);
    for (auto id : ids)
        jobs[id].record["state"] = "Cancelled";
    for (auto id : ids)
        if (jobs.contains(id)) {
            auto &job = jobs[id];
            job.record["state"] = "Cancelled";
            if (job.request.operation != "extract")
                persist(job.request.operation == "review" ? "reviews" : "generations", job.record);
            client.cancel(id);
            if (jobs.contains(id))
                complete(id, false, QStringLiteral("用户已停止生成"));
        }
    emit changed(sid);
}
void SessionController::startRecording(QString sid, QString remoteId, QByteArray microphoneId) {
    if (live || get("sessions", sid, sid).isEmpty())
        return;
    liveSid = sid;
    remoteDevice = remoteId;
    micDevice = microphoneId;
    clock.start();
    live = true;
    pauseState = false;
    stoppingState = false;
    pendingRemote.clear();
    audioIssues.clear();
    pauseStarted = 0;
    auto s = get("sessions", sid, sid);
    s["state"] = "recording";
    persist("sessions", s);
    auto config = AppSettings().transcription();
    remoteTranscriber.connectSession(sid, "remote", config);
    micTranscriber.connectSession(sid, "microphone", config);
    audio.start(store.root(), sid, remoteId, microphoneId, 0);
    emit stateChanged();
    emit status(QStringLiteral("正在录音；转写连接状态见设备下方"));
}
void SessionController::togglePause() {
    if (!live || stoppingState)
        return;
    pauseState = !pauseState;
    if (pauseState) {
        pauseStarted = clock.elapsed();
        audio.stop();
        remoteTranscriber.finish(false);
        micTranscriber.finish(false);
    } else {
        for (auto ch : QStringList{"remote", "microphone"})
            persist("gaps", {{"id", newId()},
                             {"sessionId", liveSid},
                             {"channel", ch},
                             {"startMs", pauseStarted},
                             {"endMs", clock.elapsed()},
                             {"reason", "paused"}});
        audio.start(store.root(), liveSid, remoteDevice, micDevice, clock.elapsed());
    }
    auto s = get("sessions", liveSid, liveSid);
    s["state"] = pauseState ? "paused" : "recording";
    persist("sessions", s);
    emit stateChanged();
}
void SessionController::finish() {
    if (!live || stoppingState)
        return;
    stoppingState = true;
    debounce.stop();
    deadline.stop();
    audio.stop();
    remoteTranscriber.finish();
    micTranscriber.finish();
    auto s = get("sessions", liveSid, liveSid);
    s["state"] = "stopping";
    persist("sessions", s);
    emit stateChanged();
    emit status(QStringLiteral("正在等待最终转写，最多 5 秒…"));
    QTimer::singleShot(5000, this, &SessionController::finishNow);
}
void SessionController::finishNow() {
    if (!live || !stoppingState)
        return;
    bool incomplete = false;
    for (auto ch : QStringList{"remote", "microphone"}) {
        bool pending =
            ch == "remote" ? remoteTranscriber.hasUnfinishedAudio() : micTranscriber.hasUnfinishedAudio();
        if (pending) {
            qint64 start = 0;
            for (auto t : store.list("transcripts", liveSid))
                if (t["channel"] == ch && t["final"].toBool())
                    start = qMax(start, t["endMs"].toInteger());
            persist("gaps", {{"id", newId()},
                             {"sessionId", liveSid},
                             {"channel", ch},
                             {"startMs", start},
                             {"endMs", clock.elapsed()},
                             {"reason", "unfinished"}});
            incomplete = true;
        }
        if (pauseState)
            persist("gaps", {{"id", newId()},
                             {"sessionId", liveSid},
                             {"channel", ch},
                             {"startMs", pauseStarted},
                             {"endMs", clock.elapsed()},
                             {"reason", "paused"}});
    }
    for (auto g : store.list("gaps", liveSid))
        if (g["reason"] != "paused")
            incomplete = true;
    remoteTranscriber.stop();
    micTranscriber.stop();
    auto s = get("sessions", liveSid, liveSid);
    s["state"] = "finished";
    s["endedAt"] = utcNow();
    s["durationMs"] = clock.elapsed();
    s["transcriptIncomplete"] = incomplete;
    s["audioIssues"] = QJsonArray::fromStringList(audioIssues);
    persist("sessions", s);
    live = false;
    pauseState = false;
    stoppingState = false;
    if (autoExtract && !pendingRemote.isEmpty())
        extractNow(liveSid);
    emit changed(liveSid);
    emit stateChanged();
    emit status(incomplete || !audioIssues.isEmpty()
                    ? QStringLiteral("会话已结束；录音或转写有缺失，请检查记录后复盘")
                    : QStringLiteral("面试记录已保存，可以生成复盘"));
}
void SessionController::acceptSegment(QJsonObject row) {
    if (row["sessionId"].toString() != liveSid || !live)
        return;
    auto existing = get("transcripts", liveSid, row["id"].toString());
    if (existing.value("manualEdited").toBool())
        return;
    if (!persist("transcripts", row))
        return;
    emit changed(liveSid);
    if (row["channel"] == "remote" && row["final"].toBool() && !row["text"].toString().trimmed().isEmpty()) {
        pendingRemote[row["id"].toString()] = row;
        if (autoExtract && !stoppingState) {
            debounce.start();
            if (!deadline.isActive())
                deadline.start();
        }
    }
}
void SessionController::extractNow(QString sid) {
    if (sid.isEmpty() || !activeExtraction.isEmpty())
        return;
    debounce.stop();
    deadline.stop();
    auto session = get("sessions", sid, sid);
    if (session.isEmpty())
        return;
    QList<QJsonObject> sources;
    if (sid == liveSid && !pendingRemote.isEmpty()) {
        for (auto row : pendingRemote)
            sources.append(row);
        pendingRemote.clear();
    } else {
        auto all = store.list("transcripts", sid);
        for (auto row : all)
            if (row["channel"] == "remote" && row["final"].toBool())
                sources.append(row);
        if (sources.size() > 12)
            sources = sources.mid(sources.size() - 12);
    }
    if (sources.isEmpty()) {
        emit status(QStringLiteral("暂无可提取问题的远端最终转写"));
        return;
    }
    std::stable_sort(sources.begin(), sources.end(), [](const QJsonObject &a, const QJsonObject &b) {
        return a["startMs"].toInteger() < b["startMs"].toInteger();
    });
    auto r = InterviewLogic::extraction(session, sources, AppSettings().textConfig());
    QJsonObject rec{{"state", "Running"}, {"text", ""}};
    jobs.insert(r.id, {r, rec, sources});
    activeExtraction = r.id;
    client.start(r);
    emit status(QStringLiteral("正在提取面试官问题…"));
}
void SessionController::editTranscript(QString sid, QString id, QString text) {
    auto row = get("transcripts", sid, id);
    if (row.isEmpty())
        return;
    row["text"] = text;
    row["final"] = true;
    row["manualEdited"] = true;
    row["revision"] = row["revision"].toInt() + 1;
    persist("transcripts", row);
    if (row["channel"] == "remote" && sid == liveSid)
        pendingRemote[id] = row;
    emit changed(sid);
}
void SessionController::editQuestion(QString sid, QString id, QString text) {
    auto row = get("questions", sid, id);
    if (row.isEmpty() || text.trimmed().isEmpty())
        return;
    row["text"] = text.trimmed();
    row["revision"] = row["revision"].toInt() + 1;
    row["manual"] = true;
    persist("questions", row);
    emit changed(sid);
}
