#include "app/NetworkRuntime.h"
#include "capture/WrittenInput.h"
#include "preparation/Preparation.h"
#include "ui/BossVisibility.h"
#include "ui/MainWindow.h"
#include "ui/SettingsDialog.h"
#include "ui/WrittenInterviewWindow.h"
#include <QFontDatabase>
#include <QJsonDocument>
#include <QKeySequenceEdit>
#include <QScopeGuard>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QtTest>
#include <cstring>

class WrittenReply : public QNetworkReply {
    QByteArray bytes;
    qsizetype offset = 0;
    bool ended = false;

  public:
    WrittenReply(QString text, QObject *parent, int delay = 30, bool chat = false) : QNetworkReply(parent) {
        bytes =
            chat ? "data: " +
                       QJsonDocument(
                           QJsonObject{{"choices",
                                        QJsonArray{QJsonObject{{"delta", QJsonObject{{"content", text}}}}}}})
                           .toJson(QJsonDocument::Compact) +
                       "\n\ndata: [DONE]\n\n"
                 : "data: " +
                       QJsonDocument(QJsonObject{{"type", "response.output_text.delta"}, {"delta", text}})
                           .toJson(QJsonDocument::Compact) +
                       "\n\ndata: {\"type\":\"response.completed\"}\n\n";
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        QTimer::singleShot(delay, this, [this] {
            if (ended)
                return;
            emit readyRead();
            ended = true;
            setFinished(true);
            emit finished();
        });
    }
    void abort() override {
        if (ended)
            return;
        ended = true;
        setError(OperationCanceledError, "cancelled");
        setFinished(true);
        emit finished();
    }
    qint64 bytesAvailable() const override {
        return bytes.size() - offset + QNetworkReply::bytesAvailable();
    }
    qint64 readData(char *out, qint64 count) override {
        auto n = qMin(count, qint64(bytes.size() - offset));
        if (!n)
            return -1;
        std::memcpy(out, bytes.constData() + offset, n);
        offset += n;
        return n;
    }
};
class WrittenIdleSource : public AudioSource {
    void run() override {
        QAudioFormat format;
        format.setSampleRate(24000);
        format.setChannelCount(1);
        format.setSampleFormat(QAudioFormat::Int16);
        emit formatReady(format);
        exec();
    }
};

class WrittenTest : public QObject {
    Q_OBJECT
    QTemporaryDir profile{QDir::currentPath() + "/written-XXXXXX"};
  private slots:
    void initTestCase() {
        QVERIFY(profile.isValid());
        QCoreApplication::setOrganizationName("InterviewWrittenTests");
        QCoreApplication::setApplicationName("Written");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile.path());
        QStandardPaths::setTestModeEnabled(true);
        NetworkRuntime::initializeTls();
        QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");
    }
    void init() {
        QSettings().clear();
        for (const auto &key : {"OpenAI/url", "DeepSeek/url", "transcription/url"})
            QSettings().setValue(key, "https://written-fixture.invalid");
    }
    void entryHasTwoPanesAndEndReturnsWithoutLosingState() {
        const auto capture = [](quintptr, bool) { return WindowCaptureResult{true, {}}; };
        MainWindow main(nullptr, profile.path() + "/data-" + newId(), capture, capture,
                        [](int, quint32, quint32, bool) { return HotkeyResult{true, {}}; });
        main.show();
        QTest::qWait(30);
        auto entry = main.findChild<QPushButton *>("writtenInterviewButton");
        QVERIFY(entry);
        main.findChild<QCheckBox *>("transparentBackground")->setChecked(true);
        main.findChild<QPushButton *>("capturePrivacyToggle")->click();
        const auto mainHandle = main.winId();
        entry->click();
        QPointer<QWidget> written = main.findChild<QWidget *>("writtenInterviewWindow");
        QVERIFY(written);
        QVERIFY(written->isVisible());
        QVERIFY2(static_cast<WrittenInterviewWindow *>(written.data())->statusBar()->height() < 60,
                 "The status bar must not consume the question/answer panes");
        QVERIFY(!main.isVisible());
        auto split = written->findChild<QSplitter *>("writtenSplitter");
        QVERIFY(split);
        QCOMPARE(split->orientation(), Qt::Vertical);
        QCOMPARE(split->count(), 2);
        QVERIFY(written->findChild<QComboBox *>("writtenModelCombo"));
        auto end = written->findChild<QPushButton *>("endWrittenInterview");
        QVERIFY(end);
        end->click();
        QTRY_VERIFY(!written);
        QVERIFY(main.isVisible());
        QCOMPARE(main.winId(), mainHandle);
        QVERIFY(main.findChild<QCheckBox *>("transparentBackground")->isChecked());
        QVERIFY(main.findChild<QPushButton *>("capturePrivacyToggle")->isChecked());
    }
    void capturedImageAppearsAsAnImageQuestion() {
        const auto success = [](quintptr, bool) { return WindowCaptureResult{true, {}}; };
        MainWindow main(nullptr, profile.path() + "/image-ui-" + newId(), success, success,
                        [](int, quint32, quint32, bool) { return HotkeyResult{true, {}}; });
        auto controller = main.findChild<SessionController *>();
        QVERIFY(controller);
        controller->client.keyReader = [](const QString &) { return QString(); };
        main.show();
        QTest::qWait(20);
        main.findChild<QPushButton *>("writtenInterviewButton")->click();
        auto written = main.findChild<WrittenInterviewWindow *>();
        QVERIFY(written);
        auto preview = written->findChild<QLabel *>("writtenImagePreview");
        QVERIFY(preview);
        auto input = written->findChild<WrittenInput *>();
        QVERIFY(input);
        QImage image(120, 80, QImage::Format_RGB32);
        image.fill(Qt::cyan);
        input->imageCaptured(image);
        QVERIFY(preview->isVisible());
        const auto sessions = controller->store.list("sessions");
        QCOMPARE(sessions.size(), 1);
        const auto questions = controller->store.list("questions", sessions[0]["id"].toString());
        QCOMPARE(questions.size(), 1);
        QVERIFY(!questions[0]["screenshot"].toObject()["path"].toString().isEmpty());
        written->close();
    }
    void imageAnswerUsesResumeAndSurvivesProviderSwitch() {
        const auto data = profile.path() + "/image-answer-" + newId();
        QVERIFY(QDir().mkpath(data));
        QVERIFY(Preparation::save(
            data, {{"role", "Embedded"},
                   {"resume", QJsonObject{{"fileName", "fixture.txt"}, {"text", "RESUME_SPI_PROJECT"}}}}));
        QSettings().setValue("OpenAI/model", "fixture-vision");
        QSettings().setValue("DeepSeek/model", "deepseek-flash");
        const auto success = [](quintptr, bool) { return WindowCaptureResult{true, {}}; };
        MainWindow main(nullptr, data, success, success,
                        [](int, quint32, quint32, bool) { return HotkeyResult{true, {}}; });
        auto controller = main.findChild<SessionController *>();
        QVERIFY(controller);
        QList<QJsonObject> requests;
        controller->client.keyReader = [](const QString &) { return QString("fixture-only-key"); };
        controller->client.postRequest = [&](const QNetworkRequest &, const QByteArray &body) {
            requests.append(QJsonDocument::fromJson(body).object());
            return new WrittenReply(requests.size() == 1 ? "FIRST image answer" : "SECOND image answer",
                                    &controller->client, 30, requests.last().contains("messages"));
        };
        main.show();
        QTest::qWait(20);
        main.findChild<QPushButton *>("writtenInterviewButton")->click();
        auto written = main.findChild<WrittenInterviewWindow *>();
        QVERIFY(written);
        QImage image(160, 100, QImage::Format_RGB32);
        image.fill(Qt::yellow);
        written->findChild<WrittenInput *>()->imageCaptured(image);
        QTRY_COMPARE(requests.size(), 1);
        const auto blocks = requests[0]["input"].toArray().last().toObject()["content"].toArray();
        QCOMPARE(blocks.size(), 2);
        QVERIFY(blocks[0].toObject()["text"].toString().contains("RESUME_SPI_PROJECT"));
        QVERIFY(blocks[1].toObject()["image_url"].toString().startsWith("data:image/png;base64,"));
        auto answer = written->findChild<QTextBrowser *>("writtenAnswer");
        QVERIFY(answer);
        QTRY_VERIFY(answer->toPlainText().contains("FIRST image answer"));
        auto models = written->findChild<QComboBox *>("writtenModelCombo");
        QVERIFY(models);
        models->setCurrentIndex(models->findData("DeepSeek"));
        QTRY_COMPARE(requests.size(), 2);
        QCOMPARE(requests[1]["model"].toString(), QString("deepseek-flash"));
        const auto second = requests[1]["messages"].toArray().last().toObject()["content"].toArray();
        QCOMPARE(second[1].toObject()["image_url"].toObject()["url"].toString(),
                 blocks[1].toObject()["image_url"].toString());
        QTRY_VERIFY(answer->toPlainText().contains("SECOND image answer"));
        QCOMPARE(controller->store.list("questions", controller->store.list("sessions")[0]["id"].toString())
                     .size(),
                 1);
        const auto preview = qEnvironmentVariable("INTERVIEW_WRITTEN_SCREENSHOT");
        if (!preview.isEmpty()) {
            written->setAppearance(true, 80);
            QTest::qWait(30);
            QVERIFY(written->grab().save(preview));
        }
        written->close();
        QCOMPARE(main.findChild<QComboBox *>("providerCombo")->currentText(), QString("DeepSeek"));
    }
    void textQuestionDoesNotReuseAnImageWithTheSamePrompt() {
        QSettings().setValue("OpenAI/model", "fixture-vision");
        const auto success = [](quintptr, bool) { return WindowCaptureResult{true, {}}; };
        MainWindow main(nullptr, profile.path() + "/image-to-text-" + newId(), success, success,
                        [](int, quint32, quint32, bool) { return HotkeyResult{true, {}}; });
        auto controller = main.findChild<SessionController *>();
        QVERIFY(controller);
        QList<QJsonObject> requests;
        controller->client.keyReader = [](const QString &) { return QString("fixture-only-key"); };
        controller->client.postRequest = [&](const QNetworkRequest &, const QByteArray &body) {
            requests.append(QJsonDocument::fromJson(body).object());
            return new WrittenReply("fixture answer", &controller->client);
        };
        main.show();
        QTest::qWait(20);
        main.findChild<QPushButton *>("writtenInterviewButton")->click();
        auto written = main.findChild<WrittenInterviewWindow *>();
        QVERIFY(written);
        QImage image(120, 80, QImage::Format_RGB32);
        image.fill(Qt::cyan);
        written->acceptImage(image);
        QTRY_COMPARE(requests.size(), 1);
        const auto prompt = written->findChild<QPlainTextEdit *>("writtenQuestion")->toPlainText();
        written->acceptQuestion(prompt);
        QTRY_COMPARE(requests.size(), 2);
        QVERIFY2(!QJsonDocument(requests[1]).toJson().contains("input_image"),
                 "A text-only capture must not resend the earlier screenshot");
        QVERIFY(!written->findChild<QLabel *>("writtenImagePreview")->isVisible());
        const auto sid = controller->store.list("sessions")[0]["id"].toString();
        QCOMPARE(controller->store.list("questions", sid).size(), 2);
        written->close();
    }
    void olderStreamDoesNotAppearUnderNewQuestion() {
        QSettings().setValue("OpenAI/model", "fixture");
        const auto success = [](quintptr, bool) { return WindowCaptureResult{true, {}}; };
        MainWindow main(nullptr, profile.path() + "/stream-" + newId(), success, success,
                        [](int, quint32, quint32, bool) { return HotkeyResult{true, {}}; });
        auto c = main.findChild<SessionController *>();
        QVERIFY(c);
        c->client.keyReader = [](const QString &) { return QString("fixture-key"); };
        int calls = 0;
        c->client.postRequest = [&](const QNetworkRequest &, const QByteArray &) {
            ++calls;
            return new WrittenReply(calls == 1 ? "OLD answer" : "NEW answer", &c->client,
                                    calls == 1 ? 400 : 30);
        };
        main.show();
        QTest::qWait(20);
        main.findChild<QPushButton *>("writtenInterviewButton")->click();
        auto written = main.findChild<WrittenInterviewWindow *>();
        QVERIFY(written);
        written->acceptQuestion("old question");
        written->acceptQuestion("new question");
        auto answer = written->findChild<QTextBrowser *>("writtenAnswer");
        bool stale = false;
        connect(answer->document(), &QTextDocument::contentsChanged, this,
                [&] { stale |= answer->toPlainText().contains("OLD answer"); });
        QTRY_VERIFY(answer->toPlainText().contains("NEW answer"));
        QVERIFY(!stale);
        written->close();
    }
    void recordingAndPauseStateSurviveModeRoundTrip() {
        const auto success = [](quintptr, bool) { return WindowCaptureResult{true, {}}; };
        MainWindow main(nullptr, profile.path() + "/recording-" + newId(), success, success,
                        [](int, quint32, quint32, bool) { return HotkeyResult{true, {}}; });
        auto c = main.findChild<SessionController *>();
        QVERIFY(c);
        c->autoExtract = c->autoAnswer = false;
        c->audio.renderFactory = [](const QString &) -> AudioSource * { return new WrittenIdleSource; };
        c->audio.microphoneFactory = [](const QByteArray &) -> AudioSource * {
            return new WrittenIdleSource;
        };
        const auto cleanup = qScopeGuard([&] {
            if (c->recording())
                c->discardRecording();
        });
        main.show();
        QTest::qWait(20);
        main.findChild<QPushButton *>("startButton")->click();
        QVERIFY(c->recording());
        c->togglePause();
        QVERIFY(c->paused());
        const auto id = c->liveSessionId();
        main.findChild<QPushButton *>("writtenInterviewButton")->click();
        auto written = main.findChild<WrittenInterviewWindow *>();
        QVERIFY(written);
        QVERIFY(c->recording());
        QVERIFY(c->paused());
        QCOMPARE(c->liveSessionId(), id);
        written->close();
        QTRY_VERIFY(main.isVisible());
        QVERIFY(c->recording());
        QVERIFY(c->paused());
        QCOMPARE(c->liveSessionId(), id);
    }
    void captureShortcutsAreConfigurableAndRejectDuplicates() {
        SettingsDialog settings;
        auto copy = settings.findChild<QKeySequenceEdit *>("selectionCopySequence");
        auto shot = settings.findChild<QKeySequenceEdit *>("regionCaptureSequence");
        auto save = settings.findChild<QPushButton *>("applyCaptureShortcuts");
        QVERIFY(copy && shot && save);
        QCOMPARE(copy->keySequence(), QKeySequence("Alt+C"));
        QCOMPARE(shot->keySequence(), QKeySequence("Ctrl+Alt+A"));
        copy->setKeySequence(QKeySequence("Alt+F8"));
        shot->setKeySequence(QKeySequence("Ctrl+Alt+F9"));
        save->click();
        QCOMPARE(QSettings().value("shortcuts/selectionCopy").toString(), QString("Alt+F8"));
        QCOMPARE(QSettings().value("shortcuts/regionCapture").toString(), QString("Ctrl+Alt+F9"));
        shot->setKeySequence(QKeySequence("Alt+F8"));
        save->click();
        QCOMPARE(QSettings().value("shortcuts/regionCapture").toString(), QString("Ctrl+Alt+F9"));
    }
    void bossKeyRestoresOnlyCurrentModeWindow() {
        const auto success = [](quintptr, bool) { return WindowCaptureResult{true, {}}; };
        MainWindow main(nullptr, profile.path() + "/mode-" + newId(), success, success,
                        [](int, quint32, quint32, bool) { return HotkeyResult{true, {}}; });
        main.show();
        QTest::qWait(20);
        main.findChild<QPushButton *>("writtenInterviewButton")->click();
        auto written = main.findChild<WrittenInterviewWindow *>();
        auto key = main.findChild<GlobalHotkey *>();
        QVERIFY(written && key);
        key->activated();
        QVERIFY(!written->isVisible());
        key->activated();
        QVERIFY(written->isVisible());
        QVERIFY(!main.isVisible());
        written->close();
        QTRY_VERIFY(main.isVisible());
    }
    void destroyedWrittenWindowRestoresMainDuringShellCallback() {
        MainWindow *root = nullptr;
        quintptr rootId = 0;
        const auto capture = [](quintptr, bool) { return WindowCaptureResult{true, {}}; };
        MainWindow main(
            nullptr, profile.path() + "/reentrant-" + newId(), capture,
            [&](quintptr id, bool on) {
                if (!on && id != rootId && root)
                    delete root->findChild<WrittenInterviewWindow *>();
                return WindowCaptureResult{true, {}};
            },
            [](int, quint32, quint32, bool) { return HotkeyResult{true, {}}; });
        root = &main;
        main.show();
        QTest::qWait(20);
        rootId = main.winId();
        main.findChild<QPushButton *>("writtenInterviewButton")->click();
        QTRY_VERIFY(main.isVisible());
        QVERIFY(!main.findChild<WrittenInterviewWindow *>());
    }
    void mainTaskbarStaysHiddenUntilWrittenModeEnds() {
        QHash<quintptr, bool> flags;
        const auto success = [](quintptr, bool) { return WindowCaptureResult{true, {}}; };
        MainWindow main(
            nullptr, profile.path() + "/shell-mode-" + newId(), success,
            [&](quintptr id, bool excluded) {
                flags[id] = excluded;
                return WindowCaptureResult{true, {}};
            },
            [](int, quint32, quint32, bool) { return HotkeyResult{true, {}}; });
        main.show();
        QTest::qWait(20);
        const auto root = quintptr(main.winId());
        auto privacy = main.findChild<QPushButton *>("capturePrivacyToggle");
        privacy->click();
        main.findChild<QPushButton *>("writtenInterviewButton")->click();
        auto written = main.findChild<WrittenInterviewWindow *>();
        QVERIFY(written);
        const auto child = quintptr(written->winId());
        privacy->click();
        QVERIFY(flags.value(root));
        QVERIFY(!flags.value(child, true));
        written->close();
        QTRY_VERIFY(main.isVisible());
        QVERIFY(!flags.value(root, true));
    }
};
QTEST_MAIN(WrittenTest)
#include "tst_written.moc"
