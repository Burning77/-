#include "ui/MainWindow.h"
#include <QComboBox>
#include <QFontDatabase>
#include <QListWidget>
#include <QPushButton>
#include <QtTest>
class WindowTest : public QObject {
    Q_OBJECT
  private slots:
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
