#include "ui/MainWindow.h"
#include "ui/SettingsDialog.h"
#include "app/AppSettings.h"
#include "preparation/Preparation.h"
#include <QTableWidget>
#include <QComboBox>
#include <QFontDatabase>
#include <QInputDialog>
#include <QTimer>
#include <QTabWidget>
#include <QScreen>
#include <QWindow>
#include <QMessageBox>
#include <QtTest>
static QPushButton *button(QWidget &w, const QString &text) {
    for (auto b : w.findChildren<QPushButton *>())
        if (b->text() == text)
            return b;
    return nullptr;
}
static QListWidget *questionList(QWidget &w) {
    for (auto list : w.findChildren<QListWidget *>())
        if (list->objectName() != "transcriptList")
            return list;
    return nullptr;
}
static void selectSession(QWidget &w, const QString &sid) {
    auto findCombo = [&]() -> QComboBox * {
        for (auto combo : w.findChildren<QComboBox *>())
            if (combo->findData(sid) >= 0)
                return combo;
        return nullptr;
    };
    QTRY_VERIFY_WITH_TIMEOUT(findCombo(), 1000);
    for (auto combo : w.findChildren<QComboBox *>()) {
        int i = combo->findData(sid);
        if (i >= 0) {
            combo->setCurrentIndex(i);
            return;
        }
    }
}
#include <QListWidget>
#include <QPushButton>
class WindowIdleSource : public AudioSource {
    void run() override {
        QAudioFormat format;
        format.setSampleRate(24000);
        format.setChannelCount(1);
        format.setSampleFormat(QAudioFormat::Int16);
        emit formatReady(format);
        exec();
    }
};
class WindowTest : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() {
        QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/msyh.ttc"));
        QApplication::setFont(QFont("Microsoft YaHei", 9));
    }
    void firstShowCentersOnPrimaryAndKeepsLaterMoves() {
        QTemporaryDir dir(QDir::currentPath() + "/position-XXXXXX");
        MainWindow w(nullptr, dir.path());
        auto primary = QGuiApplication::primaryScreen();
        QVERIFY(primary);
        const auto available = primary->availableGeometry();
        w.move(available.topLeft() + QPoint(17, 19));
        w.show();
        QTest::qWait(60);
        QTRY_COMPARE(w.windowHandle()->screen(), primary);
        QTRY_VERIFY((w.frameGeometry().center() - available.center()).manhattanLength() <= 2);
        QVERIFY2(available.contains(w.frameGeometry()),
                 "Initial frame must fit inside the primary work area");
        w.move(available.topLeft() + QPoint(31, 37));
        const auto moved = w.pos();
        w.hide();
        w.show();
        QTest::qWait(30);
        QCOMPARE(w.pos(), moved);
    }
    void reShowingOffscreenWindowReturnsItToPrimary() {
        QTemporaryDir dir(QDir::currentPath() + "/recover-show-XXXXXX");
        MainWindow w(nullptr, dir.path());
        w.show();
        QTest::qWait(60);
        const auto available = QGuiApplication::primaryScreen()->availableGeometry();
        w.hide();
        w.move(available.right() + 2000, available.bottom() + 2000);
        w.show();
        QTest::qWait(80);
        QTRY_VERIFY_WITH_TIMEOUT(available.contains(w.frameGeometry()), 500);
    }
    void visibleMaximizedWindowKeepsItsStateOnActivation() {
        QTemporaryDir dir(QDir::currentPath() + "/visible-maximized-XXXXXX");
        MainWindow w(nullptr, dir.path());
        w.show();
        QTest::qWait(60);
        w.showMaximized();
        QTest::qWait(30);
        QVERIFY(w.isMaximized());
        QEvent activated(QEvent::WindowActivate);
        QCoreApplication::sendEvent(&w, &activated);
        QTest::qWait(80);
        QVERIFY(w.isMaximized());
    }
    void geometryAfterMaximizeCanStillBeRecovered() {
        QTemporaryDir dir(QDir::currentPath() + "/recover-maximized-XXXXXX");
        MainWindow w(nullptr, dir.path());
        w.show();
        QTest::qWait(60);
        const auto available = QGuiApplication::primaryScreen()->availableGeometry();
        w.showMaximized();
        QTest::qWait(30);
        w.move(available.right() + 2000, available.bottom() + 2000);
        QVERIFY(!available.contains(w.frameGeometry().center()));
        QEvent activated(QEvent::WindowActivate);
        QCoreApplication::sendEvent(&w, &activated);
        QTest::qWait(80);
        QTRY_VERIFY_WITH_TIMEOUT(available.contains(w.frameGeometry()), 500);
    }
    void activatingOffscreenWindowReturnsItToPrimary() {
        QTemporaryDir dir(QDir::currentPath() + "/recover-activate-XXXXXX");
        MainWindow w(nullptr, dir.path());
        w.show();
        QTest::qWait(60);
        const auto available = QGuiApplication::primaryScreen()->availableGeometry();
        w.move(available.right() + 2000, available.bottom() + 2000);
        QEvent activated(QEvent::WindowActivate);
        QCoreApplication::sendEvent(&w, &activated);
        QTest::qWait(80);
        QTRY_VERIFY_WITH_TIMEOUT(available.contains(w.frameGeometry()), 500);
    }
    void startInterviewIncludesPreparedCompanyAndGlossary() {
        QTemporaryDir dir(QDir::currentPath() + "/start-prepared-XXXXXX");
        QJsonObject prepared{{"company", QStringLiteral("开始路径测试公司")},
                             {"role", "Qt"},
                             {"terms", QJsonArray{QJsonObject{{"term", "Qt"},
                                                              {"aliases", QJsonArray{QStringLiteral("丘特")}},
                                                              {"meaning", "framework"}}}}};
        prepared["termsBasis"] = Preparation::fingerprint(prepared);
        QVERIFY(Preparation::save(dir.path(), prepared));
        MainWindow w(nullptr, dir.path());
        auto c = w.findChild<SessionController *>();
        c->autoExtract = false;
        c->autoAnswer = false;
        c->audio.renderFactory = [](const QString &) -> AudioSource * { return new WindowIdleSource; };
        c->audio.microphoneFactory = [](const QByteArray &) -> AudioSource * { return new WindowIdleSource; };
        w.findChild<QPushButton *>("startButton")->click();
        QVERIFY(c->recording());
        auto snapshot = c->store.list("sessions", c->liveSessionId()).first()["preparation"].toObject();
        QCOMPARE(snapshot["company"].toString(), QStringLiteral("开始路径测试公司"));
        c->findChildren<RealtimeTranscriber *>().first()->segmentChanged({{"id", "term"},
                                                                          {"sessionId", c->liveSessionId()},
                                                                          {"channel", "remote"},
                                                                          {"final", true},
                                                                          {"text", QStringLiteral("丘特")}});
        QCOMPARE(c->store.list("transcripts", c->liveSessionId()).first()["text"].toString(), QString("Qt"));
        QVERIFY(c->discardRecording());
    }
    void preparationDialogSavesDraftAndNewSessionGetsSnapshot() {
        QTemporaryDir dir(QDir::currentPath() + "/preparation-ui-XXXXXX");
        MainWindow w(nullptr, dir.path());
        auto prepare = w.findChild<QPushButton *>("preparationButton");
        QVERIFY(prepare);
        QTimer::singleShot(0, &w, [] {
            auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            auto company = dialog->findChild<QLineEdit *>("prepCompany"),
                 role = dialog->findChild<QLineEdit *>("prepRole");
            QVERIFY(company);
            QVERIFY(role);
            company->setText(QStringLiteral("测试科技"));
            role->setText("Qt engineer");
            auto terms = dialog->findChild<QTableWidget *>("prepTerms");
            QVERIFY(terms);
            terms->setRowCount(1);
            terms->setItem(0, 0, new QTableWidgetItem("Qt"));
            terms->setItem(0, 1, new QTableWidgetItem("Q T"));
            terms->setItem(0, 2, new QTableWidgetItem(QStringLiteral("界面框架")));
            QVERIFY(dialog->grab().save(QDir::currentPath() + "/preparation-preview.png"));
            auto save = dialog->findChild<QPushButton *>("savePreparation");
            QVERIFY(save);
            save->click();
        });
        prepare->click();
        auto draft = Preparation::load(dir.path());
        QCOMPARE(draft["company"].toString(), QStringLiteral("测试科技"));
        QLineEdit *manual = nullptr;
        for (auto line : w.findChildren<QLineEdit *>())
            if (line->placeholderText().startsWith(QStringLiteral("手动输入问题")))
                manual = line;
        QVERIFY(manual);
        manual->setText("Qt?");
        auto controller = w.findChild<SessionController *>();
        controller->client.keyReader = [](QString) { return QString(); };
        button(w, QStringLiteral("添加问题并分析"))->click();
        auto sessions = controller->store.list("sessions");
        QCOMPARE(sessions.size(), 1);
        QCOMPARE(sessions.first()["preparation"].toObject()["company"].toString(),
                 QStringLiteral("测试科技"));
        QCOMPARE(sessions.first()["role"].toString(), QString("Qt engineer"));
    }
    void discardConfirmationTargetsLiveSessionAndKeepsOtherHistory() {
        QTemporaryDir dir(QDir::currentPath() + "/discard-window-XXXXXX");
        MainWindow w(nullptr, dir.path());
        auto c = w.findChild<SessionController *>();
        c->autoExtract = false;
        c->audio.renderFactory = [](const QString &) -> AudioSource * { return new WindowIdleSource; };
        c->audio.microphoneFactory = [](const QByteArray &) -> AudioSource * { return new WindowIdleSource; };
        auto saved = c->createSession("saved-history", "", "");
        auto current = c->createSession("current-recording", "", "");
        c->startRecording(current, "fixture", {});
        selectSession(w, saved);
        auto discard = w.findChild<QPushButton *>("discardButton");
        QVERIFY(discard);
        QVERIFY(discard->isEnabled());
        auto respond = [&](QMessageBox::StandardButton response) {
            QTimer::singleShot(0, &w, [response] {
                auto box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                QVERIFY(box);
                QVERIFY(box->text().contains("current-recording"));
                box->button(response)->click();
            });
            QTest::mouseClick(discard, Qt::LeftButton);
        };
        respond(QMessageBox::No);
        QVERIFY(c->recording());
        QCOMPARE(c->store.list("sessions").size(), 2);
        respond(QMessageBox::Yes);
        QVERIFY(!c->recording());
        QCOMPARE(c->store.list("sessions").size(), 1);
        QCOMPARE(c->store.list("sessions").first()["id"].toString(), saved);
        QVERIFY(!discard->isEnabled());
        c->changed(current);
        QTest::qWait(150);
        QCOMPARE(c->store.list("sessions").first()["id"].toString(), saved);
    }
    void offersChatGptLoginAndLocalSpeech() {
        QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/msyh.ttc"));
        QApplication::setFont(QFont("Microsoft YaHei", 9));
        QTemporaryDir dir(QDir::currentPath() + "/window-XXXXXX");
        MainWindow w(nullptr, dir.path());
        auto provider = w.findChild<QComboBox *>("providerCombo");
        QVERIFY(provider->findText("ChatGPT") >= 0);
        SettingsDialog settings;
        QVERIFY(settings.findChild<QPushButton *>("chatgptLoginButton"));
        QVERIFY(settings.findChild<QPushButton *>("deepSeekLoginButton"));
        QVERIFY(settings.findChild<QComboBox *>("transcriptionMode"));
        settings.show();
        QTest::qWait(60);
        QVERIFY(settings.grab().save(QDir::currentPath() + "/settings-preview.png"));
        auto tabs = settings.findChild<QTabWidget *>();
        QVERIFY(tabs);
        tabs->setCurrentIndex(2);
        auto mode = settings.findChild<QComboBox *>("deepSeekMode");
        QVERIFY(mode);
        mode->setCurrentIndex(0);
        QTest::qWait(30);
        QVERIFY(settings.size().height() <= QGuiApplication::primaryScreen()->availableGeometry().height());
        QVERIFY(settings.grab().save(QDir::currentPath() + "/deepseek-settings-preview.png"));
    }
    void batchesRepeatedRefreshNotifications() {
        QTemporaryDir dir(QDir::currentPath() + "/window-XXXXXX");
        MainWindow w(nullptr, dir.path());
        auto c = w.findChild<SessionController *>();
        auto sid = c->createSession("refresh", "", "");
        selectSession(w, sid);
        auto list = w.findChild<QListWidget *>("transcriptList");
        QSignalSpy resets(list->model(), &QAbstractItemModel::modelReset);
        for (int i = 0; i < 300; ++i)
            c->changed(sid);
        QCOMPARE(resets.size(), 0);
        QTRY_VERIFY_WITH_TIMEOUT(!resets.isEmpty(), 1000);
        QVERIFY(resets.size() <= 2);
    }
    void mergesLegacyGapFloodForDisplay() {
        QTemporaryDir dir(QDir::currentPath() + "/window-XXXXXX");
        MainWindow w(nullptr, dir.path());
        auto c = w.findChild<SessionController *>();
        auto sid = c->createSession("legacy", "", "");
        for (int i = 0; i < 300; ++i)
            QVERIFY(c->store.put("gaps", {{"id", newId()},
                                          {"sessionId", sid},
                                          {"channel", "remote"},
                                          {"startMs", i * 10},
                                          {"endMs", (i + 1) * 10}}));
        selectSession(w, sid);
        auto list = w.findChild<QListWidget *>("transcriptList");
        QTRY_VERIFY_WITH_TIMEOUT(list->count() > 0, 1000);
        QCOMPARE(list->count(), 1);
    }
    void oldAnswerCallbackDoesNotExitNewDraft() {
        QTemporaryDir dir(QDir::currentPath() + "/draft-XXXXXX");
        MainWindow w(nullptr, dir.path());
        auto c = w.findChild<SessionController *>();
        QVERIFY(c);
        c->client.keyReader = [](const QString &) { return QString(); };
        auto old = c->createSession("old", "", "");
        selectSession(w, old);
        QTest::mouseClick(button(w, QStringLiteral("新会话")), Qt::LeftButton);
        c->changed(old);
        QLineEdit *manual = nullptr;
        for (auto line : w.findChildren<QLineEdit *>())
            if (line->placeholderText().startsWith(QStringLiteral("手动输入问题")))
                manual = line;
        QVERIFY(manual);
        manual->setText("new-question");
        QTest::mouseClick(button(w, QStringLiteral("添加问题并分析")), Qt::LeftButton);
        QCOMPARE(c->store.list("sessions").size(), 2);
        QVERIFY(c->store.list("questions", old).isEmpty());
    }
    void editingKeepsIdentityAcrossModalRefresh() {
        QTemporaryDir dir(QDir::currentPath() + "/edit-XXXXXX");
        MainWindow w(nullptr, dir.path());
        auto c = w.findChild<SessionController *>();
        QVERIFY(c);
        auto sid = c->createSession("edit", "", "");
        auto q = c->addQuestion(sid, "original", false);
        selectSession(w, sid);
        auto list = questionList(w);
        QVERIFY(list);
        list->setCurrentRow(0);
        QTimer::singleShot(0, &w, [&] {
            auto dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            c->changed(sid);
            list->clear();
            dialog->setTextValue("edited");
            dialog->accept();
        });
        QTest::mouseClick(button(w, QStringLiteral("编辑问题")), Qt::LeftButton);
        auto rows = c->store.list("questions", sid);
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows[0]["id"].toString(), q);
        QCOMPARE(rows[0]["text"].toString(), QString("edited"));
    }
    void startsIdle() {
        QTemporaryDir dir(QDir::currentPath() + "/window-XXXXXX");
        QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/msyh.ttc"));
        MainWindow w(nullptr, dir.path());
        auto start = w.findChild<QPushButton *>("startButton");
        QVERIFY2(start, "The idle window must offer a start action");
        QVERIFY(start->isEnabled());
        QVERIFY(w.findChild<QPushButton *>("settingsButton"));
        QVERIFY(w.findChild<QComboBox *>("providerCombo"));
        QVERIFY(w.findChild<QListWidget *>("transcriptList"));
        w.show();
        QTest::qWait(60);
        QVERIFY(w.grab().save(QDir::currentPath() + "/window-preview.png"));
    }
};
QTEST_MAIN(WindowTest)
#include "tst_window.moc"
