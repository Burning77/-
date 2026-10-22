#include "transcription/LocalTranscriber.h"
#include "audio/PcmConverter.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QtEndian>
#include <QtTest>
class LocalAsrTest : public QObject {
    Q_OBJECT
    QString modelPath() const {
        auto packaged = qEnvironmentVariable("INTERVIEW_PACKAGED_ASR_ROOT");
        return packaged.isEmpty()
                   ? qEnvironmentVariable("INTERVIEW_LOCAL_ASR_ROOT") + "/vosk-model-small-cn-0.22"
                   : packaged + "/model";
    }
    QString libraryPath() const {
        auto packaged = qEnvironmentVariable("INTERVIEW_PACKAGED_ASR_ROOT");
        return packaged.isEmpty()
                   ? qEnvironmentVariable("INTERVIEW_LOCAL_ASR_ROOT") + "/vosk-win64-0.3.45/libvosk.dll"
                   : packaged + "/libvosk.dll";
    }
  private slots:
    void partialsReplaceAndFinalsKeepIdentity() {
        LocalResultAssembler a;
        auto first = a.apply("s", "remote", QStringLiteral("互斥"), false, 0, 100);
        QVERIFY(!first.isEmpty());
        auto second = a.apply("s", "remote", QStringLiteral("互斥锁"), false, 0, 200);
        QCOMPARE(second["id"], first["id"]);
        QCOMPARE(second["text"].toString(), QStringLiteral("互斥锁"));
        auto final = a.apply("s", "remote", QStringLiteral("互斥锁是什么"), true, 0, 300);
        QCOMPARE(final["id"], first["id"]);
        QVERIFY(final["final"].toBool());
        auto next = a.apply("s", "remote", QStringLiteral("下一题"), true, 400, 500);
        QVERIFY(next["id"] != final["id"]);
    }
    void missingEngineFailsAsynchronously() {
        LocalTranscriber t;
        QSignalSpy status(&t, &LocalTranscriber::status);
        QSignalSpy gaps(&t, &LocalTranscriber::gap);
        QElapsedTimer clock;
        clock.start();
        t.connectSession("s", "remote", "missing-model", "missing-runtime");
        t.appendAudio(QByteArray(24000, 0), 0);
        QVERIFY(clock.elapsed() < 200);
        QTRY_VERIFY_WITH_TIMEOUT(!status.isEmpty(), 3000);
        QVERIFY(status.last()[1].toString().contains(QStringLiteral("失败")));
        QVERIFY(!t.hasUnfinishedAudio());
        t.appendAudio(QByteArray(24000, 0), 500);
        QVERIFY(!gaps.isEmpty());
        QCOMPARE(gaps.first()[1].toLongLong(), 0);
        QCOMPARE(gaps.last()[2].toLongLong(), 1000);
    }
    void realModelLoadsAndQueueIsBounded() {
        auto root = qEnvironmentVariable("INTERVIEW_LOCAL_ASR_ROOT");
        if (root.isEmpty())
            QSKIP("prepare local ASR dependencies and set INTERVIEW_LOCAL_ASR_ROOT");
        LocalTranscriber t;
        QSignalSpy status(&t, &LocalTranscriber::status), gaps(&t, &LocalTranscriber::gap);
        QElapsedTimer clock;
        clock.start();
        t.connectSession("fixture", "remote", modelPath(), libraryPath());
        QVERIFY(clock.elapsed() < 200);
        for (int i = 0; i < 600; ++i)
            t.appendAudio(QByteArray(4800, 0), i * 100);
        QVERIFY(t.bufferedMilliseconds() <= 30000);
        QVERIFY(!gaps.isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(
            [&]() {
                for (auto row : status)
                    if (row[1].toString().contains(QStringLiteral("就绪")))
                        return true;
                return false;
            }(),
            20000);
        t.finish();
        QTRY_VERIFY_WITH_TIMEOUT(!t.hasUnfinishedAudio(), 15000);
        t.stop();
    }
    void publicChineseSpeechProducesFinalText() {
        auto sample = qEnvironmentVariable("INTERVIEW_CHINESE_TEST_WAV");
        if (sample.isEmpty())
            QSKIP("Optional public Chinese test WAV not installed");
        QFile file(sample);
        QVERIFY(file.open(QIODevice::ReadOnly));
        auto wav = file.readAll();
        QVERIFY(wav.startsWith("RIFF") && wav.mid(8, 4) == "WAVE");
        QByteArray pcm;
        for (qsizetype offset = 12; offset + 8 <= wav.size();) {
            auto size = qFromLittleEndian<quint32>(wav.constData() + offset + 4);
            QVERIFY(size <= quint32(wav.size() - offset - 8));
            if (wav.mid(offset, 4) == "fmt ") {
                QVERIFY(size >= 16);
                QCOMPARE(qFromLittleEndian<quint16>(wav.constData() + offset + 8), 1);
                QCOMPARE(qFromLittleEndian<quint16>(wav.constData() + offset + 10), 1);
                QCOMPARE(qFromLittleEndian<quint32>(wav.constData() + offset + 12), 16000u);
                QCOMPARE(qFromLittleEndian<quint16>(wav.constData() + offset + 22), 16);
            }
            if (wav.mid(offset, 4) == "data")
                pcm = wav.mid(offset + 8, size);
            offset += 8 + size + (size & 1);
        }
        QVERIFY(!pcm.isEmpty());
        QAudioFormat format;
        format.setSampleRate(16000);
        format.setChannelCount(1);
        format.setSampleFormat(QAudioFormat::Int16);
        PcmConverter converter(format);
        pcm = converter.process(pcm) + QByteArray(96000, 0);
        LocalTranscriber t;
        QSignalSpy rows(&t, &LocalTranscriber::segmentChanged);
        t.connectSession("public-sample", "remote", modelPath(), libraryPath());
        t.appendAudio(pcm, 0);
        t.finish();
        QTRY_VERIFY_WITH_TIMEOUT(!t.hasUnfinishedAudio(), 30000);
        QString text;
        for (auto row : rows) {
            auto object = row[0].toJsonObject();
            if (object["final"].toBool())
                text += object["text"].toString();
        }
        qInfo().noquote() << "Public Chinese speech:" << text;
        QVERIFY2(!text.isEmpty(), "Real Chinese speech must produce a final transcript");
    }
};
QTEST_MAIN(LocalAsrTest)
#include "tst_localasr.moc"
