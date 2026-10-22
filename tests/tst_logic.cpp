#include "ai/InterviewLogic.h"
#include "preparation/Preparation.h"
#include <QtTest>
class LogicTest : public QObject {
    Q_OBJECT
  private slots:
    void preparationReachesExtractionAnswersAndReviews() {
        QJsonObject prep{{"company", QStringLiteral("示例芯片公司")},
                         {"role", "Firmware"},
                         {"jobDescription", "RTOS C++"},
                         {"terms", QJsonArray{QJsonObject{{"term", "RTOS"},
                                                          {"meaning", QStringLiteral("实时操作系统")},
                                                          {"aliases", QJsonArray{}}}}}};
        prep["termsBasis"] = Preparation::fingerprint(prep);
        QJsonObject session{{"id", "s"}, {"role", "Firmware"}, {"preparation", prep}};
        QList<QJsonObject> segments{{{"id", "speech"},
                                     {"sessionId", "s"},
                                     {"channel", "remote"},
                                     {"final", true},
                                     {"text", "RTOS?"}}};
        for (auto request : {InterviewLogic::extraction(session, segments, {}),
                             InterviewLogic::answer(session, {{"id", "q"}, {"text", "RTOS?"}}, segments, {}),
                             InterviewLogic::review(session, {}, segments, {})}) {
            auto body = QString::fromUtf8(QJsonDocument(request.messages).toJson());
            QVERIFY(body.contains(QStringLiteral("示例芯片公司")));
            QVERIFY(body.contains(QStringLiteral("实时操作系统")));
        }
    }

    void terminologyRawTextCannotDisplaceReviewEvidence() {
        QJsonObject session{{"id", "s"}};
        QList<QJsonObject> segments;
        for (int i = 0; i < 6; i++)
            segments.append({{"id", QString("speech-%1").arg(i)},
                             {"sessionId", "s"},
                             {"channel", "microphone"},
                             {"final", true},
                             {"text", QString("UNIQUE-%1-").arg(i) + QString(1500, QChar('A' + i))},
                             {"terminologyAdjusted", true},
                             {"rawText", QString(5000, 'R')}});
        for (auto chunk : InterviewLogic::reviewChunks({}, segments)) {
            auto request = InterviewLogic::review(session, {}, chunk.sources, {});
            auto input = request.messages.last().toObject()["content"].toString();
            for (auto source : chunk.sources)
                QVERIFY2(input.contains(source["text"].toString()),
                         "Raw transcript annotation must not displace any chunk's evidence text");
        }
    }
    void boundsEntireReviewAndPreservesOversizedSpeech() {
        QString speech(26000, QChar('A'));
        speech.append("THE-END");
        QJsonObject session{{"id", "s"}, {"role", QString(50000, 'R')}};
        QList<QJsonObject> segments{{{"id", "spoken"},
                                     {"sessionId", "s"},
                                     {"channel", "microphone"},
                                     {"final", true},
                                     {"revision", 7},
                                     {"text", speech}}};
        QList<QJsonObject> questions;
        for (int i = 0; i < 500; i++)
            questions.append({{"id", QString::number(i)},
                              {"text", QString(2000, 'Q')},
                              {"sourceIds", QJsonArray{"spoken"}}});
        auto chunks = InterviewLogic::reviewChunks(questions, segments);
        QVERIFY(chunks.size() > 1);
        QString recovered;
        for (auto chunk : chunks) {
            auto request = InterviewLogic::review(session, chunk.questions, chunk.sources, {});
            int size = 0;
            for (auto message : request.messages)
                size += message.toObject()["content"].toString().size();
            QVERIFY(size <= 12000);
            for (auto row : chunk.sources)
                recovered += row["text"].toString();
        }
        QCOMPARE(recovered, speech);
    }
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
