#include "ui/MainWindow.h"
#include "ui/SettingsDialog.h"
#include "app/AppSettings.h"
#include <QComboBox>
#include <QFontDatabase>
#include <QInputDialog>
#include <QTimer>
#include <QScreen>
#include <QWindow>
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
class WindowTest : public QObject {
    Q_OBJECT
  private slots:
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
        w.move(available.topLeft() + QPoint(31, 37));
        const auto moved = w.pos();
        w.hide();
        w.show();
        QTest::qWait(30);
        QCOMPARE(w.pos(), moved);
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
        QVERIFY(settings.findChild<QComboBox *>("transcriptionMode"));
        settings.show();
        QTest::qWait(60);
        QVERIFY(settings.grab().save(QDir::currentPath() + "/settings-preview.png"));
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
