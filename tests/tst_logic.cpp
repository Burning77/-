#include "ai/InterviewLogic.h"
#include <QtTest>
class LogicTest : public QObject {
    Q_OBJECT
  private slots:
    void validatesQuestionSources() {
        QJsonObject s{{"id", "s"}};
        QList<QJsonObject> t{QJsonObject{{"id", "t"},
                                         {"sessionId", "s"},
                                         {"channel", "remote"},
                                         {"final", true},
                                         {"revision", 1},
                                         {"text", QStringLiteral("什么是互斥锁？")}}};
        QString e;
        auto q = InterviewLogic::parseQuestions(
            R"({"questions":[{"text":"What is a mutex?","sourceIds":["t"]}]})", s, t, &e);
        QCOMPARE(q.size(), 1);
        QCOMPARE(q[0]["sessionId"].toString(), QString("s"));
        q = InterviewLogic::parseQuestions(R"({"questions":[{"text":"bad","sourceIds":["unknown"]}]})", s, t,
                                           &e);
        QVERIFY(q.isEmpty());
        QVERIFY(!e.isEmpty());
        q = InterviewLogic::parseQuestions("{", s, t, &e);
        QVERIFY(q.isEmpty());
        QVERIFY(!e.isEmpty());
    }
    void separatesActualSpeechAndReferenceAnswers() {
        QJsonObject s{{"id", "s"}, {"role", QStringLiteral("嵌入式")}};
        QList<QJsonObject> t{QJsonObject{{"id", "t"},
                                         {"sessionId", "s"},
                                         {"channel", "microphone"},
                                         {"final", true},
                                         {"text", "actual answer"},
                                         {"startMs", 100}}};
        auto r = InterviewLogic::review(s, {}, t, {});
        auto text = QString::fromUtf8(QJsonDocument(r.messages).toJson());
        QVERIFY(text.contains("actual answer"));
        QVERIFY(text.contains("t"));
        QCOMPARE(r.operation, QString("review"));
        auto md = InterviewLogic::markdown(
            s, t, {},
            {{{"id", "a"}, {"text", "reference answer"}, {"provider", "DeepSeek"}, {"model", "test"}}}, {});
        QVERIFY(md.contains("actual answer"));
        QVERIFY(md.contains("reference answer"));
        QVERIFY(md.contains(QStringLiteral("AI 参考答案")));
    }
};
QTEST_MAIN(LogicTest)
#include "tst_logic.moc"
