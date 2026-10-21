#include "audio/PcmConverter.h"
#include "audio/WavWriter.h"
#include "audio/AudioController.h"
#include <QtEndian>
#include <QtTest>
class SyntheticSource : public AudioSource {
  protected:
    void run() override {
        QAudioFormat f;
        f.setSampleRate(24000);
        f.setChannelCount(1);
        f.setSampleFormat(QAudioFormat::Int16);
        emit formatReady(f);
        QByteArray b;
        for (int i = 0; i < 240; i++) {
            char p[2];
            qToLittleEndian<qint16>(1234, p);
            b.append(p, 2);
        }
        emit pcm(b);
        if (!isInterruptionRequested())
            exec();
    }
};
class AudioTest : public QObject {
    Q_OBJECT
  private slots:
    void ignoresQueuedAudioFromPreviousCapture() {
        QTemporaryDir d(QDir::currentPath() + "/capture-XXXXXX");
        AudioController audio;
        AudioSource *source = nullptr;
        audio.renderFactory = [&](const QString &) -> AudioSource * {
            source = new SyntheticSource;
            return source;
        };
        audio.microphoneFactory = [](const QByteArray &) -> AudioSource * { return nullptr; };
        QSignalSpy chunks(&audio, &AudioController::pcmReady);
        audio.start(d.path(), "old", "", {}, 0);
        QTRY_VERIFY(!chunks.isEmpty());
        chunks.clear();
        QAudioFormat f;
        f.setSampleRate(24000);
        f.setChannelCount(1);
        f.setSampleFormat(QAudioFormat::Int16);
        source->formatReady(f);
        QByteArray stale;
        for (int i = 0; i < 240; i++) {
            char p[2];
            qToLittleEndian<qint16>(30000, p);
            stale.append(p, 2);
        }
        source->pcm(stale);
        audio.stop();
        audio.start(d.path(), "new", "", {}, 1000);
        QTRY_VERIFY(!chunks.isEmpty());
        for (auto chunk : chunks)
            QCOMPARE(qFromLittleEndian<qint16>(chunk[1].toByteArray().constData()), qint16(1234));
        audio.stop();
    }
    void convertsStereoWithoutBreakingChunkBoundaries() {
        QAudioFormat f;
        f.setSampleRate(48000);
        f.setChannelCount(2);
        f.setSampleFormat(QAudioFormat::Float);
        PcmConverter c(f);
        QByteArray b;
        for (int i = 0; i < 480; i++) {
            float left = 0.5f, right = 0.5f;
            b.append(reinterpret_cast<char *>(&left), 4);
            b.append(reinterpret_cast<char *>(&right), 4);
        }
        QByteArray out;
        for (int i = 0; i < b.size(); i += 13)
            out += c.process(b.mid(i, 13));
        QCOMPARE(out.size(), 480);
        QCOMPARE(qFromLittleEndian<qint16>(out.constData()), qint16(16384));
    }
    void writesPlayablePcmHeader() {
        auto path = QDir::currentPath() + "/audio-test.wav";
        WavWriter w;
        QVERIFY(w.open(path));
        QVERIFY(w.append(QByteArray(480, 0)));
        w.close();
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        auto b = f.readAll();
        QCOMPARE(b.size(), 524);
        QCOMPARE(b.left(4), QByteArray("RIFF"));
        QCOMPARE(qFromLittleEndian<quint32>(b.constData() + 24), quint32(24000));
        QCOMPARE(qFromLittleEndian<quint32>(b.constData() + 40), quint32(480));
        f.close();
        QFile::remove(path);
    }
};
QTEST_MAIN(AudioTest)
#include "tst_audio.moc"
