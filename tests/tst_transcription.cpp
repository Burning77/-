#include "transcription/TranscriptAssembler.h"
#include "transcription/RealtimeTranscriber.h"
#include <QtTest>
class TranscriptionTest : public QObject {
    Q_OBJECT
  private slots:
    void reportsBufferedAudioAsUnfinishedWhenNoConnection() {
        RealtimeTranscriber transcriber;
        ProviderConfig config;
        config.provider = "Transcription";
        config.baseUrl = "invalid";
        transcriber.connectSession("session", "remote", config);
        transcriber.appendAudio(QByteArray(4800, 0), 0);
        transcriber.finish();
        QVERIFY(transcriber.hasUnfinishedAudio());
        transcriber.stop();
        QVERIFY(!transcriber.hasUnfinishedAudio());
    }
    void reconcilesPartialsAndFinalsWithoutDuplicates() {
        TranscriptAssembler a;
        auto p = a.apply({{"type", "conversation.item.input_audio_transcription.delta"},
                          {"item_id", "i"},
                          {"delta", QStringLiteral("互斥")}},
                         "s", "remote", 100, 200);
        QCOMPARE(p["text"].toString(), QStringLiteral("互斥"));
        QVERIFY(!p["final"].toBool());
        auto f = a.apply({{"type", "conversation.item.input_audio_transcription.completed"},
                          {"item_id", "i"},
                          {"transcript", QStringLiteral("互斥锁是什么？")}},
                         "s", "remote", 100, 500);
        QCOMPARE(f["id"], p["id"]);
        QVERIFY(f["final"].toBool());
        QCOMPARE(f["startMs"].toInteger(), qint64(100));
        auto duplicate = a.apply({{"type", "conversation.item.input_audio_transcription.completed"},
                                  {"item_id", "i"},
                                  {"transcript", QStringLiteral("互斥锁是什么？")}},
                                 "s", "remote", 100, 500);
        QVERIFY(duplicate.isEmpty());
        auto late = a.apply({{"type", "conversation.item.input_audio_transcription.delta"},
                             {"item_id", "i"},
                             {"delta", "late"}},
                            "s", "remote", 100, 500);
        QVERIFY(late.isEmpty());
    }
};
QTEST_MAIN(TranscriptionTest)
#include "tst_transcription.moc"
