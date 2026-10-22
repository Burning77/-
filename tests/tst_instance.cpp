#include "app/InstanceGuard.h"
#include <QProcess>
#include <QtTest>
class InstanceTest : public QObject {
    Q_OBJECT
  private slots:
    void excludesAnotherProcessAndReleasesOnExit() {
        QTemporaryDir dir;
        auto path = dir.path() + "/instance.lock";
        {
            InstanceGuard first(path);
            QVERIFY(first.acquire());
            QProcess other;
            other.start(QCoreApplication::applicationFilePath(), {"--probe-lock", path});
            QVERIFY(other.waitForFinished(5000));
            QCOMPARE(other.exitCode(), 2);
        }
        QProcess after;
        after.start(QCoreApplication::applicationFilePath(), {"--probe-lock", path});
        QVERIFY(after.waitForFinished(5000));
        QCOMPARE(after.exitCode(), 0);
    }
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().size() == 3 && app.arguments()[1] == "--probe-lock") {
        InstanceGuard guard(app.arguments()[2]);
        return guard.acquire() ? 0 : 2;
    }
    InstanceTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_instance.moc"
