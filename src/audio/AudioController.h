#pragma once
#include "MicrophoneSource.h"
#include "PcmConverter.h"
#include "WasapiLoopbackSource.h"
#include "WavWriter.h"
#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <memory>
#include <functional>
class AudioController : public QObject {
    Q_OBJECT
  public:
    explicit AudioController(QObject *p = nullptr);
    std::function<AudioSource *(const QString &)> renderFactory;
    std::function<AudioSource *(const QByteArray &)> microphoneFactory;
    ~AudioController() {
        stop();
    }
    void start(const QString &root, const QString &sessionId, const QString &remoteId,
               const QByteArray &micId, qint64 offsetMs);
    void stop();
    bool running() const {
        return active;
    }
  signals:
    void pcmReady(QString channel, QByteArray pcm, qint64 startMs);
    void levelChanged(QString channel, float level);
    void channelError(QString channel, QString message);
    void segmentSaved(QJsonObject segment);

  private:
    void format(QString channel, QAudioFormat format);
    void receive(QString channel, const QByteArray &bytes);
    struct Track {
        std::unique_ptr<PcmConverter> converter;
        std::unique_ptr<WavWriter> writer;
        QJsonObject record;
    };
    std::shared_ptr<Track> remote, microphone;
    AudioSource *loop = nullptr;
    AudioSource *mic = nullptr;
    QString root, sid;
    qint64 offset = 0;
    QElapsedTimer clock;
    bool active = false;
    quint64 generation = 0;
};
