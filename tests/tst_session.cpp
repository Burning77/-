#include "app/SessionController.h"
#include <QtTest>
class SessionTest : public QObject {
    Q_OBJECT
  private slots:
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
