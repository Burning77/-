#include "storage/SessionStore.h"
#include <QTemporaryDir>
#include <QtTest>
class StorageTest : public QObject {
    Q_OBJECT
  private slots:
    void persistsRevisionsAcrossReopen() {
        QTemporaryDir dir(QDir::currentPath() + "/storage-XXXXXX");
        {
            SessionStore s;
            QVERIFY2(s.open(dir.path()), qPrintable(s.lastError()));
            QVERIFY(s.put("sessions", {{"id", "s1"}, {"title", QStringLiteral("面试")}}));
            QVERIFY(
                s.put("transcripts", {{"id", "t1"}, {"sessionId", "s1"}, {"text", "old"}, {"revision", 1}}));
            QVERIFY(
                s.put("transcripts", {{"id", "t1"}, {"sessionId", "s1"}, {"text", "new"}, {"revision", 2}}));
        }
        SessionStore s;
        QVERIFY(s.open(dir.path()));
        auto rows = s.list("transcripts", "s1");
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.first()["text"].toString(), QString("new"));
        QVERIFY(!s.removeSession("../"));
        QVERIFY(!s.put("evil", {{"id", "bad"}}));
        QVERIFY(s.removeSession("s1"));
        QVERIFY(s.list("transcripts", "s1").isEmpty());
    }
    void refusesCrossSessionOverwrite() {
        QTemporaryDir d(QDir::currentPath() + "/storage-XXXXXX");
        SessionStore s;
        QVERIFY2(s.open(d.path()), qPrintable(s.lastError()));
        QVERIFY(s.put("sessions", {{"id", "a"}}));
        QVERIFY(s.put("sessions", {{"id", "b"}}));
        QVERIFY(s.put("questions", {{"id", "q"}, {"sessionId", "a"}, {"text", "A"}}));
        QVERIFY(!s.put("questions", {{"id", "q"}, {"sessionId", "b"}, {"text", "B"}}));
    }
};
QTEST_MAIN(StorageTest)
#include "tst_storage.moc"
