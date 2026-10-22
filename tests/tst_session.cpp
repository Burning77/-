#include "app/SessionController.h"
#include "app/AppSettings.h"
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>
class IdleSource : public AudioSource {
    void run() override {
        QAudioFormat f;
        f.setSampleRate(24000);
        f.setChannelCount(1);
        f.setSampleFormat(QAudioFormat::Int16);
        emit formatReady(f);
        exec();
    }
};
class SessionTest : public QObject {
    Q_OBJECT
    QTemporaryDir settings;
  private slots:
    void discardsOnlyLiveSession_data() {
        QTest::addColumn<bool>("paused");
        QTest::newRow("recording") << false;
        QTest::newRow("paused") << true;
    }
    void discardsOnlyLiveSession() {
        QFETCH(bool, paused);
        QTemporaryDir data(QDir::currentPath() + "/discard-XXXXXX");
        SessionController c(data.path());
        c.autoExtract = false;
        c.autoAnswer = false;
        c.client.keyReader = [](const QString &) { return QString(); };
        c.audio.renderFactory = [](const QString &) -> AudioSource * { return new IdleSource; };
        c.audio.microphoneFactory = [](const QByteArray &) -> AudioSource * { return new IdleSource; };
        auto saved = c.createSession("saved", "", "");
        auto sid = c.createSession("discard", "", "");
        c.startRecording(sid, "fixture", {});
        QTRY_COMPARE(c.store.list("audio", sid).size(), 2);
        auto transcriber = c.findChildren<RealtimeTranscriber *>().first();
        const QJsonObject speech{{"id", "speech"},     {"sessionId", sid}, {"channel", "remote"},
                                 {"text", "question"}, {"final", true},    {"revision", 1}};
        transcriber->segmentChanged(speech);
        transcriber->gap("remote", 0, 100);
        if (paused)
            c.togglePause();
        auto q = c.addQuestion(sid, "question", false);
        auto request = c.answerQuestion(sid, q, ProviderConfig());
        c.client.textDelta(request, "partial answer");
        QVERIFY(QFileInfo::exists(data.path() + "/sessions/" + sid));
        bool removed = false;
        QVERIFY(QMetaObject::invokeMethod(&c, "discardRecording", Qt::DirectConnection,
                                          Q_RETURN_ARG(bool, removed)));
        QVERIFY(removed);
        QVERIFY(!c.recording());
        QVERIFY(!c.audio.running());
        QVERIFY(c.liveSessionId().isEmpty());
        QVERIFY(!QFileInfo::exists(data.path() + "/sessions/" + sid));
        c.client.textDelta(request, "late answer");
        c.client.finished(request);
        transcriber->segmentChanged(speech);
        transcriber->gap("remote", 100, 200);
        QTest::qWait(650);
        for (auto kind : {"transcripts", "questions", "generations", "reviews", "audio", "gaps"})
            QVERIFY(c.store.list(kind, sid).isEmpty());
        QCOMPARE(c.store.list("sessions").size(), 1);
        QCOMPARE(c.store.list("sessions").first()["id"].toString(), saved);
    }
    void failedLocalRecognizerPersistsIncompleteSession() {
        QTemporaryDir data;
        SessionController c(data.path());
        c.autoExtract = false;
        c.audio.renderFactory = [](const QString &) -> AudioSource * { return new IdleSource; };
        c.audio.microphoneFactory = [](const QByteArray &) -> AudioSource * { return new IdleSource; };
        const auto mode = AppSettings().transcriptionMode(), model = AppSettings().localModelPath();
        AppSettings().saveLocalTranscription("Local", "missing-model");
        auto sid = c.createSession("recognizer-failure", "", "");
        QSignalSpy status(&c, &SessionController::transcriptionStatus);
        c.startRecording(sid, "fixture", {});
        AppSettings().saveLocalTranscription(mode, model);
        QTRY_VERIFY_WITH_TIMEOUT(!status.isEmpty(), 3000);
        c.audio.pcmReady("remote", QByteArray(48000, 0), 0);
        c.finish();
        QTRY_VERIFY_WITH_TIMEOUT(!c.recording(), 6500);
        auto session = c.store.list("sessions").first();
        QVERIFY(session["transcriptIncomplete"].toBool());
        QVERIFY(!session["transcriptionIssues"].toArray().isEmpty());
        QVERIFY(!c.store.list("gaps", sid).isEmpty());
    }
    void coalescesContinuousGapNotifications() {
        QTemporaryDir data;
        SessionController c(data.path());
        c.audio.renderFactory = [](const QString &) -> AudioSource * { return new IdleSource; };
        c.audio.microphoneFactory = [](const QByteArray &) -> AudioSource * { return new IdleSource; };
        auto sid = c.createSession("overflow", "", "");
        c.startRecording(sid, "fixture", {});
        QSignalSpy changes(&c, &SessionController::changed);
        auto transcriber = c.findChildren<RealtimeTranscriber *>().first();
        for (int i = 0; i < 300; ++i)
            transcriber->gap("remote", i * 10, (i + 1) * 10);
        QVERIFY(changes.size() <= 1);
        QTRY_COMPARE_WITH_TIMEOUT(c.store.list("gaps", sid).size(), 1, 2000);
        auto gap = c.store.list("gaps", sid).first();
        QCOMPARE(gap["startMs"].toInteger(), qint64(0));
        QCOMPARE(gap["endMs"].toInteger(), qint64(3000));
        QVERIFY(changes.size() <= 2);
    }
    void batchesStreamingUpdatesAndFlushesCompletion() {
        QTemporaryDir data;
        SessionController c(data.path());
        c.client.keyReader = [](const QString &) { return QString(); };
        auto sid = c.createSession("stream", "", "");
        auto qid = c.addQuestion(sid, "question", false);
        auto request = c.answerQuestion(sid, qid, ProviderConfig());
        QSignalSpy changes(&c, &SessionController::changed);
        for (int i = 0; i < 300; ++i)
            c.client.textDelta(request, "a");
        QVERIFY(changes.size() <= 1);
        c.client.finished(request);
        auto rows = c.store.list("generations", sid);
        QCOMPARE(rows.first()["text"].toString(), QString(300, 'a'));
        QCOMPARE(rows.first()["state"].toString(), QString("Completed"));
    }
    void initTestCase() {
        QCoreApplication::setOrganizationName("InterviewAssistantTests");
        QCoreApplication::setApplicationName("SessionRegression");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        ProviderConfig transcription;
        transcription.baseUrl = "invalid://test-disabled";
        AppSettings().saveTranscription(transcription);
    }
    void extractsFinalPendingSpeechAfterRecordingEnds() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QList<QTcpSocket *> requests;
        auto respond = [](QTcpSocket *socket) {
            socket->write(
                "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nConnection: close\r\n\r\ndata: "
                "{\"choices\":[{\"delta\":{\"content\":\"{\\\"questions\\\":[]}\"},\"finish_reason\":"
                "\"stop\"}]}\n\ndata: [DONE]\n\n");
            socket->disconnectFromHost();
        };
        connect(&server, &QTcpServer::newConnection, this, [&] {
            auto socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                socket->readAll();
                if (requests.contains(socket))
                    return;
                requests.append(socket);
                if (requests.size() > 1)
                    respond(socket);
            });
        });
        ProviderConfig config;
        config.provider = "DeepSeek";
        config.model = "fixture";
        config.baseUrl = QString("http://127.0.0.1:%1").arg(server.serverPort());
        AppSettings().save(config);
        AppSettings().select("DeepSeek");
        config.baseUrl = "invalid://disabled";
        AppSettings().saveTranscription(config);
        QTemporaryDir data;
        SessionController c(data.path());
        c.client.keyReader = [](const QString &) { return QString("test-only"); };
        c.audio.renderFactory = [](const QString &) -> AudioSource * { return new IdleSource; };
        c.audio.microphoneFactory = [](const QByteArray &) -> AudioSource * { return new IdleSource; };
        auto sid = c.createSession("final speech", "", "");
        c.startRecording(sid, "fixture", {});
        auto transcriber = c.findChildren<RealtimeTranscriber *>().first();
        QJsonObject row{{"id", "first"}, {"sessionId", sid}, {"channel", "remote"},
                        {"final", true}, {"revision", 1},    {"text", "first question"}};
        transcriber->segmentChanged(row);
        c.extractNow(sid);
        QTRY_COMPARE_WITH_TIMEOUT(requests.size(), 1, 3000);
        row["id"] = "last";
        row["text"] = "last question";
        transcriber->segmentChanged(row);
        c.finish();
        QTRY_VERIFY_WITH_TIMEOUT(!c.recording(), 6500);
        respond(requests.first());
        QTRY_COMPARE_WITH_TIMEOUT(requests.size(), 2, 3000);
    }
    void rejectsMixedValidAndInventedReviewCitations() {
        QTemporaryDir d(QDir::currentPath() + "/review-XXXXXX");
        SessionController c(d.path());
        c.client.keyReader = [](const QString &) { return QString(); };
        auto sid = c.createSession("review", "", "");
        QVERIFY(c.store.put("transcripts", {{"id", "t"},
                                            {"sessionId", sid},
                                            {"channel", "microphone"},
                                            {"final", true},
                                            {"revision", 7},
                                            {"text", "actual answer"}}));
        auto request = c.review(sid);
        QVERIFY(!request.isEmpty());
        c.client.textDelta(request, "[t] correct reference; [invented-id] fabricated reference");
        c.client.finished(request);
        auto rows = c.store.list("reviews", sid);
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows[0]["state"].toString(), QString("Failed"));
    }
    void preservesReviewSourceVersions() {
        QTemporaryDir d(QDir::currentPath() + "/review-version-XXXXXX");
        SessionController c(d.path());
        c.client.keyReader = [](const QString &) { return QString(); };
        auto sid = c.createSession("review", "", "");
        QVERIFY(c.store.put("transcripts", {{"id", "t"},
                                            {"sessionId", sid},
                                            {"channel", "microphone"},
                                            {"final", true},
                                            {"revision", 7},
                                            {"text", "actual answer"}}));
        c.review(sid);
        auto rows = c.store.list("reviews", sid);
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows[0]["sourceRevisions"].toObject()["t"].toInt(), 7);
    }
    void preservesGenerationOriginAcrossSessionChanges() {
        QTemporaryDir d(QDir::currentPath() + "/session-XXXXXX");
        SessionController c(d.path());
        c.client.keyReader = [](const QString &) { return QString(); };
        auto a = c.createSession("A", "role", ""), b = c.createSession("B", "role", "");
        QVERIFY(!a.isEmpty());
        QVERIFY(a != b);
        auto q = c.addQuestion(a, "Explain mutexes", false);
        QVERIFY(!q.isEmpty());
        ProviderConfig config;
        config.model = "test";
        auto rid = c.answerQuestion(a, q, config);
        QVERIFY(!rid.isEmpty());
        c.client.textDelta(rid, "original answer");
        c.client.finished(rid);
        auto rows = c.store.list("generations", a);
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows[0]["questionId"].toString(), q);
        QCOMPARE(rows[0]["text"].toString(), QString("original answer"));
        QVERIFY(c.store.list("generations", b).isEmpty());
    }
    void cancelsRunningAndQueuedAnswersTogether() {
        QTemporaryDir d(QDir::currentPath() + "/cancel-XXXXXX");
        SessionController c(d.path());
        c.client.keyReader = [](const QString &) { return QString(); };
        auto s = c.createSession("A", "", "");
        ProviderConfig config;
        config.model = "test";
        auto q = c.addQuestion(s, "question", false);
        c.answerQuestion(s, q, config);
        c.answerQuestion(s, q, config);
        c.answerQuestion(s, q, config);
        c.cancel(s);
        auto rows = c.store.list("generations", s);
        QCOMPARE(rows.size(), 3);
        for (auto row : rows)
            QCOMPARE(row["state"].toString(), QString("Cancelled"));
    }
};
QTEST_MAIN(SessionTest)
#include "tst_session.moc"
