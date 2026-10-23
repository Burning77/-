#define QT_FORCE_ASSERTS
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
        return packaged.isEmpty() ? qEnvironmentVariable("INTERVIEW_LOCAL_ASR_ROOT") +
                                        "/sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20"
                                  : packaged + "/bilingual";
    }
    QString libraryPath() const {
        auto packaged = qEnvironmentVariable("INTERVIEW_PACKAGED_ASR_ROOT");
        return packaged.isEmpty()
                   ? qEnvironmentVariable("INTERVIEW_LOCAL_ASR_ROOT") +
                         "/sherpa-onnx-v1.13.8-win-x64-shared-MT-Release-no-tts/lib/sherpa-onnx-c-api.dll"
                   : packaged + "/sherpa-onnx-c-api.dll";
    }
    void loadSpeech(const QString &sample, QByteArray &pcm) {
        QFile file(sample);
        QVERIFY(file.open(QIODevice::ReadOnly));
        auto wav = file.readAll();
        QVERIFY(wav.startsWith("RIFF") && wav.mid(8, 4) == "WAVE");
        pcm.clear();
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
        QTRY_VERIFY_WITH_TIMEOUT(
            !status.isEmpty() && status.last()[1].toString().contains(QStringLiteral("失败")), 3000);
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
    void channelsFlushOnPauseAndCancellationDoesNotBlockOrEmitLateRows() {
        if (qEnvironmentVariable("INTERVIEW_LOCAL_ASR_ROOT").isEmpty())
            QSKIP("Local bilingual model not installed");
        QByteArray pcm;
        loadSpeech(qEnvironmentVariable("INTERVIEW_LOCAL_ASR_ROOT") +
                       "/sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20/test_wavs/2.wav",
                   pcm);
        QVERIFY(!pcm.isEmpty());
        pcm.chop(96000); // No artificial trailing silence: pause must flush the last word.
        LocalTranscriber remote, mic;
        QSignalSpy remoteRows(&remote, &LocalTranscriber::segmentChanged),
            micRows(&mic, &LocalTranscriber::segmentChanged);
        remote.connectSession("dual", "remote", modelPath(), libraryPath());
        mic.connectSession("dual", "mic", modelPath(), libraryPath());
        remote.appendAudio(pcm, 1000);
        mic.appendAudio(pcm, 2000);
        remote.finish(false);
        mic.finish(false);
        QTRY_VERIFY_WITH_TIMEOUT(!remote.hasUnfinishedAudio() && !mic.hasUnfinishedAudio(), 30000);
        QTRY_VERIFY(!remoteRows.isEmpty() && remoteRows.last()[0].toJsonObject()["final"].toBool());
        QTRY_VERIFY(!micRows.isEmpty() && micRows.last()[0].toJsonObject()["final"].toBool());
        QCOMPARE(remoteRows.last()[0].toJsonObject()["channel"].toString(), QString("remote"));
        QCOMPARE(micRows.last()[0].toJsonObject()["channel"].toString(), QString("mic"));
        QVERIFY(remoteRows.last()[0].toJsonObject()["text"].toString().contains("FREQUENTLY"));
        auto priorId = remoteRows.last()[0].toJsonObject().value("id").toString();
        remote.appendAudio(pcm, 10000);
        remote.finish();
        QTRY_VERIFY_WITH_TIMEOUT(!remote.hasUnfinishedAudio(), 20000);
        QTRY_VERIFY(remoteRows.last()[0].toJsonObject().value("id").toString() != priorId);
        auto count = micRows.size();
        mic.appendAudio(pcm, 10000);
        QElapsedTimer clock;
        clock.start();
        mic.stop();
        QVERIFY(clock.elapsed() < 200);
        QTest::qWait(200);
        QCOMPARE(micRows.size(), count);
    }
    void publicSpeechKeepsOriginalLanguage_data() {
        if (qEnvironmentVariable("INTERVIEW_LOCAL_ASR_ROOT").isEmpty())
            QSKIP("Prepare local bilingual ASR dependencies and test audio first");
        QTest::addColumn<QString>("sample");
        QTest::addColumn<QStringList>("expected");
        auto chinese = qEnvironmentVariable("INTERVIEW_CHINESE_TEST_WAV");
        QTest::newRow("Chinese") << chinese << QStringList{QStringLiteral("程序")};
        auto bilingual = qEnvironmentVariable("INTERVIEW_LOCAL_ASR_ROOT") +
                         "/sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20/test_wavs/";
        QTest::newRow("Mixed-Monday")
            << bilingual + "0.wav" << QStringList{"MONDAY", "AFTER TOMORROW", QStringLiteral("星期三")};
        QTest::newRow("Mixed-Always") << bilingual + "1.wav" << QStringList{"ALWAYS"};
        QTest::newRow("Mixed-Frequently") << bilingual + "2.wav" << QStringList{"FREQUENTLY"};
        QTest::newRow("Mixed-ES") << bilingual + "3.wav" << QStringList{"ES", QStringLiteral("一般现在时")};
        QTest::newRow("English") << qEnvironmentVariable("INTERVIEW_LOCAL_ASR_ROOT") + "/english-sentence.wav"
                                 << QStringList{"EARLY NIGHTFALL", "YELLOW LAMPS"};
        QTest::newRow("English-Technical")
            << qEnvironmentVariable("INTERVIEW_LOCAL_ASR_ROOT") + "/english-technical.wav"
            << QStringList{"DIRECT MEMORY ACCESS", "MUTEX"};
    }
    void publicSpeechKeepsOriginalLanguage() {
        QFETCH(QString, sample);
        QFETCH(QStringList, expected);
        if (sample.isEmpty())
            QSKIP("Optional public Chinese test WAV not installed");
        QByteArray pcm;
        loadSpeech(sample, pcm);
        QVERIFY(!pcm.isEmpty());
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
                text += object["text"].toString() + " ";
        }
        qInfo().noquote() << "Public speech:" << text;
        QVERIFY2(!text.isEmpty(), "Real speech must produce a final transcript");
        for (auto word : expected)
            QVERIFY2(text.contains(word, Qt::CaseInsensitive), qPrintable(text));
        // Test language preservation separately from word error rate: this model
        // spells the synthetic "semaphore" as SEMOPHOR. Do not claim perfect ASR.
        if (QFileInfo(sample).fileName().startsWith("english-"))
            QVERIFY2(!text.contains(QRegularExpression("[\\p{Han}]")), qPrintable(text));
    }
};
QTEST_MAIN(LocalAsrTest)
#include "tst_localasr.moc"
