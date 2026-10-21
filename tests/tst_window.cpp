#include "ui/MainWindow.h"
#include <QComboBox>
#include <QFontDatabase>
#include <QInputDialog>
#include <QTimer>
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
#include <QtTest>
class WindowTest : public QObject {
    Q_OBJECT
  private slots:
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
