#include "ai/InterviewLogic.h"
#include "preparation/Preparation.h"
#include <QtTest>
class LogicTest : public QObject {
    Q_OBJECT
  private slots:
    void referenceAnswerUsesSessionResume_data() {
        QTest::addColumn<QString>("provider");
        for (auto name : {"OpenAI", "ChatGPT", "DeepSeek"})
            QTest::newRow(name) << QString(name);
    }
    void referenceAnswerUsesSessionResume() {
        QFETCH(QString, provider);
        ProviderConfig config;
        config.provider = provider;
        const auto resumeText = QStringLiteral("参与 C6678 DSP 驱动和 Qt 上位机开发，未主导芯片选型。\n") +
                                QStringLiteral("其他简历内容。\n").repeated(1500) +
                                QStringLiteral("简历末尾：负责 FreeRTOS 任务通信。");
        const QJsonObject prep{{"company", QStringLiteral("测试公司")},
                               {"resume", QJsonObject{{"fileName", "candidate.pdf"}, {"text", resumeText}}}};
        const QJsonObject session{{"id", "s"},
                                  {"role", "Firmware"},
                                  {"experience", "OUTDATED-BACKGROUND-MUST-NOT-REPLACE-RESUME"},
                                  {"preparation", prep}};
        const QJsonObject question{{"id", "q"}, {"text", QStringLiteral("讲讲你在 DSP 项目里的职责")}};
        const auto request = InterviewLogic::answer(session, question, {}, config);
        const auto input = request.messages.last().toObject()["content"].toString();
        QVERIFY2(input.contains(resumeText),
                 "Reference answers must include the complete session resume, not just the old summary");
        QVERIFY(input.contains("candidate.pdf"));
        QVERIFY(!input.contains("OUTDATED-BACKGROUND-MUST-NOT-REPLACE-RESUME"));
        QVERIFY(input.contains(question["text"].toString()));
        const auto instruction = request.messages.first().toObject()["content"].toString();
        QVERIFY(instruction.contains(QStringLiteral("优先结合简历")));
        QVERIFY(instruction.contains(QStringLiteral("不虚构")));
        QVERIFY(instruction.contains(QStringLiteral("不输出分析过程")));
        QVERIFY(instruction.contains(QStringLiteral("第一人称")));
        QCOMPARE(request.config.provider, provider);
        QCOMPARE(request.sessionId, QString("s"));
        QCOMPARE(request.questionId, QString("q"));
    }
    void referenceAnswerCanUseLegacyPreparationBackground() {
        const QJsonObject prep{{"experience", QStringLiteral("旧版真实经历：参与 RTOS 驱动开发")}};
        const QJsonObject session{{"id", "s"}, {"preparation", prep}};
        const auto request = InterviewLogic::answer(session, {{"id", "q"}, {"text", "RTOS?"}}, {}, {});
        QVERIFY(request.messages.last().toObject()["content"].toString().contains(
            QStringLiteral("旧版真实经历：参与 RTOS 驱动开发")));
    }
    void referenceAnswerUsesShortCandidateSpeech_data() {
        QTest::addColumn<QString>("provider");
        for (auto name : {"OpenAI", "ChatGPT", "DeepSeek"})
            QTest::newRow(name) << QString(name);
    }
    void referenceAnswerUsesShortCandidateSpeech() {
        QFETCH(QString, provider);
        ProviderConfig config;
        config.provider = provider;
        QJsonObject session{{"id", "session"},
                            {"role", QStringLiteral("嵌入式工程师")},
                            {"experience", QStringLiteral("只参与驱动开发，没有主导芯片选型")}};
        QJsonObject question{{"id", "question"}, {"text", QStringLiteral("为什么选用这个芯片？")}};
        auto request = InterviewLogic::answer(session, question, {}, config);
        auto instruction = request.messages.first().toObject()["content"].toString();
        QVERIFY(instruction.contains(QStringLiteral("第一人称")));
        QVERIFY(instruction.contains(QStringLiteral("口述")));
        QVERIFY(instruction.contains(QStringLiteral("用中文")));
        QVERIFY(instruction.contains(QStringLiteral("只输出")));
        QVERIFY(instruction.contains(QStringLiteral("2至4句")));
        QVERIFY(instruction.contains(QStringLiteral("80至160字")));
        QVERIFY(instruction.contains(QStringLiteral("不输出分析过程")));
        QVERIFY(instruction.contains(QStringLiteral("不虚构")));
        QVERIFY(!instruction.contains(QStringLiteral("先提供可快速阅读的3至5个要点")));
        QVERIFY(!instruction.contains(QStringLiteral("再解释原理、例子和可能的追问")));
        QVERIFY(!instruction.contains(QStringLiteral("信息不足时给可填写的表达框架")));
        auto input = request.messages.last().toObject()["content"].toString();
        QVERIFY(input.contains(session["experience"].toString()));
        QVERIFY(input.contains(question["text"].toString()));
        QCOMPARE(request.config.provider, provider);
        QCOMPARE(request.operation, QString("answer"));
        QCOMPARE(request.sessionId, QString("session"));
        QCOMPARE(request.questionId, QString("question"));
        QVERIFY(!request.jsonMode);
    }
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
