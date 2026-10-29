#include "SessionController.h"
#include "capture/ScreenshotStore.h"
#include <QFile>
#include "ai/InterviewLogic.h"
#include "preparation/Preparation.h"
#include "app/AppSettings.h"
#include <QDir>
#include <QJsonDocument>
#include <QSet>
#include <QStandardPaths>
#include <QRegularExpression>
#include <algorithm>
SessionController::SessionController(QString root, QObject *p)
    : QObject(p), client(this), audio(this), remoteTranscriber(this), micTranscriber(this), localRemote(this),
      localMic(this) {
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
    connect(&debounce, &QTimer::timeout, this, &SessionController::extractPending);
    connect(&deadline, &QTimer::timeout, this, &SessionController::extractPending);
    gapFlushTimer.setSingleShot(true);
    gapFlushTimer.setInterval(500);
    connect(&gapFlushTimer, &QTimer::timeout, this, &SessionController::flushGaps);
    generationFlushTimer.setSingleShot(true);
    generationFlushTimer.setInterval(100);
    connect(&generationFlushTimer, &QTimer::timeout, this, &SessionController::flushGenerations);
    connect(&client, &TextClient::textDelta, this, [this](QString id, QString text) {
        if (!jobs.contains(id))
            return;
        auto &job = jobs[id];
        if (job.record.value("state") == "Cancelled")
            return;
        job.record["text"] = job.record["text"].toString() + text;
        if (job.request.operation != "extract") {
            dirtyJobs.insert(id);
            if (!generationFlushTimer.isActive())
                generationFlushTimer.start();
        }
    });
    connect(&client, &TextClient::finished, this, [this](QString id) { complete(id, true); });
    connect(&client, &TextClient::failed, this,
            [this](QString id, QString error) { complete(id, false, error); });
    connect(&audio, &AudioController::pcmReady, this, [this](QString ch, QByteArray pcm, qint64 ms) {
        if (usingLocalAsr)
            (ch == "remote" ? localRemote : localMic).appendAudio(pcm, ms);
        else
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
        connect(t, &RealtimeTranscriber::gap, this, &SessionController::recordGap);
    }
    for (auto t : {&localRemote, &localMic}) {
        connect(t, &LocalTranscriber::segmentChanged, this, &SessionController::acceptSegment);
        connect(t, &LocalTranscriber::status, this, &SessionController::transcriptionStatus);
        connect(t, &LocalTranscriber::gap, this, &SessionController::recordGap);
        connect(t, &LocalTranscriber::failed, this, [this](QString ch, QString error) {
            if (live)
                transcriptionIssues.append(ch + ": " + error);
        });
    }
}
SessionController::~SessionController() {
    blockSignals(true);
    flushGenerations();
    flushGaps();
    audio.stop();
    remoteTranscriber.stop();
    micTranscriber.stop();
    localRemote.stop();
    localMic.stop();
}
void SessionController::recordGap(QString channel, qint64 start, qint64 end) {
    if (!live || start < 0 || end < start)
        return;
    auto previous = gapRows.value(channel);
    if (!previous.isEmpty() && start <= previous["endMs"].toInteger() + 20 &&
        end >= previous["startMs"].toInteger()) {
        previous["startMs"] = qMin(start, previous["startMs"].toInteger());
        previous["endMs"] = qMax(end, previous["endMs"].toInteger());
    } else {
        // Save a separate interval before replacing its pending record.
        if (dirtyGapChannels.contains(channel))
            persist("gaps", previous);
        previous = {{"id", newId()},
                    {"sessionId", liveSid},
                    {"channel", channel},
                    {"startMs", start},
                    {"endMs", end}};
    }
    gapRows[channel] = previous;
    dirtyGapChannels.insert(channel);
    if (!gapFlushTimer.isActive())
        gapFlushTimer.start();
}
void SessionController::flushGaps() {
    gapFlushTimer.stop();
    QSet<QString> sessions;
    const auto channels = dirtyGapChannels;
    dirtyGapChannels.clear();
    for (auto channel : channels) {
        const auto row = gapRows.value(channel);
        if (persist("gaps", row))
            sessions.insert(row["sessionId"].toString());
    }
    for (auto sid : sessions)
        emit changed(sid);
}
void SessionController::flushGenerations() {
    generationFlushTimer.stop();
    QSet<QString> sessions;
    const auto ids = dirtyJobs;
    dirtyJobs.clear();
    for (auto id : ids) {
        if (!jobs.contains(id))
            continue;
        const auto &job = jobs[id];
        if (persist(job.request.operation == "review" ? "reviews" : "generations", job.record))
            sessions.insert(job.request.sessionId);
    }
    for (auto sid : sessions)
        emit changed(sid);
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
QString SessionController::createSession(QString title, QString role, QString experience,
                                         QJsonObject preparation) {
    auto id = newId();
    if (!persist("sessions", {{"id", id},
                              {"title", title.isEmpty() ? QStringLiteral("面试记录") : title},
                              {"role", role},
                              {"experience", experience},
                              {"preparation", preparation},
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
    jobs.insert(r.id, {r, record, {}, q["screenshot"].toObject()});
    answers.enqueue(r.id);
    emit changed(sid);
    pump();
    return r.id;
}
QString SessionController::addScreenshotQuestion(QString sid, QByteArray png, QString prompt) {
    if (get("sessions", sid, sid).isEmpty())
        return {};
    const auto id = newId();
    QString error;
    const auto screenshot = ScreenshotStore::save(store.root(), sid, id, png, &error);
    if (screenshot.isEmpty()) {
        emit status(error);
        return {};
    }
    if (!persist("questions", {{"id", id},
                               {"sessionId", sid},
                               {"text", prompt},
                               {"sourceIds", QJsonArray()},
                               {"manual", true},
                               {"revision", 1},
                               {"screenshot", screenshot},
                               {"createdAt", utcNow()}})) {
        QFile::remove(store.root() + "/" + screenshot["path"].toString());
        return {};
    }
    emit changed(sid);
    return id;
}
QByteArray SessionController::screenshotForQuestion(QString sid, QString qid, QString *error) const {
    const auto question = get("questions", sid, qid);
    if (!question["screenshot"].isObject()) {
        if (error)
            error->clear();
        return {};
    }
    return ScreenshotStore::read(store.root(), sid, qid, question["screenshot"].toObject(), error);
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
        auto outgoing = job.request;
        if (!job.screenshot.isEmpty()) {
            QString error;
            outgoing.imagePng = ScreenshotStore::read(store.root(), outgoing.sessionId, outgoing.questionId,
                                                      job.screenshot, &error);
            if (outgoing.imagePng.isEmpty()) {
                complete(id, false, error);
                return;
            }
        }
        client.start(outgoing);
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
    const auto chunks = InterviewLogic::reviewChunks(store.list("questions", sid), finalized);
    QString first;
    int index = 0;
    auto config = AppSettings().textConfig();
    auto group = newId();
    for (auto chunk : chunks) {
        auto r = InterviewLogic::review(session, chunk.questions, chunk.sources, config);
        QJsonArray refs;
        QJsonObject revisions;
        QJsonArray snapshots;
        for (auto row : chunk.sources) {
            refs.append(row["id"]);
            revisions[row["id"].toString()] = row["revision"];
            snapshots.append(row);
        }
        QJsonObject rec{{"id", r.id},
                        {"sessionId", sid},
                        {"provider", config.provider},
                        {"model", config.model},
                        {"state", "Queued"},
                        {"text", ""},
                        {"sourceIds", refs},
                        {"sourceRevisions", revisions},
                        {"sourceSnapshots", snapshots},
                        {"groupId", group},
                        {"part", ++index},
                        {"totalParts", chunks.size()},
                        {"createdAt", utcNow()}};
        persist("reviews", rec);
        jobs.insert(r.id, {r, rec, chunk.sources});
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
    dirtyJobs.remove(id);
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
            QSet<QString> allowed;
            for (auto source : job.sources)
                allowed.insert(source["id"].toString());
            bool found = false, invalid = false;
            auto matches = QRegularExpression("\\[([^\\[\\]\\r\\n]+)\\]").globalMatch(text);
            while (matches.hasNext()) {
                const auto ref = matches.next().captured(1);
                if (ref == " " || ref == "x" || ref == "X")
                    continue;
                if (allowed.contains(ref))
                    found = true;
                else
                    invalid = true;
            }
            if (!found || invalid) {
                job.record["state"] = "Failed";
                job.record["error"] =
                    QStringLiteral("报告缺少实际对话引用或包含未知引用，证据校验失败，请重试");
            }
        }
        persist(job.request.operation == "review" ? "reviews" : "generations", job.record);
    }
    if (!ok && !error.isEmpty())
        emit status(error);
    emit changed(sid);
    pump();
    if (autoExtract && !pendingRemote.isEmpty())
        debounce.start();
}
void SessionController::cancel(QString sid) {
    QStringList ids;
    for (auto id : jobs.keys())
        if (jobs[id].request.sessionId == sid)
            ids.append(id);
    for (auto i = pendingRemote.begin(); i != pendingRemote.end();)
        if (i.value()["sessionId"] == sid)
            i = pendingRemote.erase(i);
        else
            ++i;
    for (auto id : ids) {
        jobs[id].record["state"] = "Cancelled";
        if (jobs[id].request.operation != "extract")
            persist(jobs[id].request.operation == "review" ? "reviews" : "generations", jobs[id].record);
    }
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
    flushGaps();
    gapRows.clear();
    liveSid = sid;
    remoteDevice = remoteId;
    micDevice = microphoneId;
    clock.start();
    live = true;
    pauseState = false;
    stoppingState = false;
    audioIssues.clear();
    transcriptionIssues.clear();
    pauseStarted = 0;
    auto s = get("sessions", sid, sid);
    s["state"] = "recording";
    persist("sessions", s);
    livePreparation = s["preparation"].toObject();
    auto config = AppSettings().transcription();
    usingLocalAsr = AppSettings().transcriptionMode() == "Local";
    if (usingLocalAsr) {
        auto model = AppSettings().localModelPath();
        localRemote.connectSession(sid, "remote", model);
        localMic.connectSession(sid, "microphone", model);
    } else {
        remoteTranscriber.connectSession(sid, "remote", config, livePreparation);
        micTranscriber.connectSession(sid, "microphone", config, livePreparation);
    }
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
        localRemote.finish(false);
        localMic.finish(false);
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
    localRemote.finish();
    localMic.finish();
    flushGaps();
    auto s = get("sessions", liveSid, liveSid);
    s["state"] = "stopping";
    persist("sessions", s);
    emit stateChanged();
    emit status(QStringLiteral("正在等待最终转写，最多 5 秒…"));
    QTimer::singleShot(5000, this, &SessionController::finishNow);
}
bool SessionController::discardRecording() {
    if (!live || stoppingState)
        return false;
    const auto sid = liveSid;
    // Reject late audio/transcription before stopping producers and closing files.
    live = false;
    pauseState = false;
    debounce.stop();
    deadline.stop();
    gapFlushTimer.stop();
    remoteTranscriber.stop();
    micTranscriber.stop();
    localRemote.stop();
    localMic.stop();
    audio.stop();
    cancel(sid);
    gapRows.clear();
    dirtyGapChannels.clear();
    liveSid.clear();
    const bool removed = store.removeSession(sid);
    if (!removed) {
        auto session = get("sessions", sid, sid);
        session["state"] = "discardFailed";
        session["discardError"] = store.lastError();
        persist("sessions", session);
    }
    emit changed(sid);
    emit stateChanged();
    emit status(removed ? QStringLiteral("本次会话已丢弃，未保留录音和记录")
                        : QStringLiteral("丢弃未完成，部分内容可能仍保留，请检查后重试删除"));
    return removed;
}
void SessionController::finishNow() {
    if (!live || !stoppingState)
        return;
    flushGaps();
    bool incomplete = !transcriptionIssues.isEmpty();
    for (auto ch : QStringList{"remote", "microphone"}) {
        bool pending = usingLocalAsr ? (ch == "remote" ? localRemote.hasUnfinishedAudio()
                                                       : localMic.hasUnfinishedAudio())
                                     : (ch == "remote" ? remoteTranscriber.hasUnfinishedAudio()
                                                       : micTranscriber.hasUnfinishedAudio());
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
    localRemote.stop();
    localMic.stop();
    auto s = get("sessions", liveSid, liveSid);
    s["state"] = "finished";
    s["endedAt"] = utcNow();
    s["durationMs"] = clock.elapsed();
    s["transcriptIncomplete"] = incomplete;
    s["audioIssues"] = QJsonArray::fromStringList(audioIssues);
    s["transcriptionIssues"] = QJsonArray::fromStringList(transcriptionIssues);
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
    row = Preparation::correct(row, livePreparation);
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
    for (auto i = pendingRemote.begin(); i != pendingRemote.end();)
        if (i.value()["sessionId"] == sid) {
            sources.append(i.value());
            i = pendingRemote.erase(i);
        } else
            ++i;
    if (sources.isEmpty()) {
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
void SessionController::extractPending() {
    if (autoExtract && !pendingRemote.isEmpty())
        extractNow(pendingRemote.constBegin().value()["sessionId"].toString());
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
