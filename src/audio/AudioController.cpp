#include "AudioController.h"
#include "domain/Types.h"
#include <QDir>
#include <QMediaDevices>
#include <QtEndian>
#include <cmath>
AudioController::AudioController(QObject *parent) : QObject(parent) {
    renderFactory = [](const QString &id) -> AudioSource * { return new WasapiLoopbackSource(id); };
    microphoneFactory = [](const QByteArray &id) -> AudioSource * {
        auto selected = QMediaDevices::defaultAudioInput();
        for (auto device : QMediaDevices::audioInputs())
            if (device.id() == id)
                selected = device;
        return selected.isNull() ? nullptr : new MicrophoneSource(selected);
    };
}
void AudioController::start(const QString &r, const QString &s, const QString &remoteId,
                            const QByteArray &micId, qint64 ms) {
    stop();
    const auto capture = generation;
    root = r;
    sid = s;
    offset = ms;
    clock.start();
    active = true;
    remote = std::make_shared<Track>();
    microphone = std::make_shared<Track>();
    loop = renderFactory(remoteId);
    if (!loop) {
        emit channelError("remote", QStringLiteral("未找到播放设备"));
        active = false;
        return;
    }
    loop->setParent(this);
    connect(
        loop, &AudioSource::formatReady, this,
        [this, capture](QAudioFormat f) {
            if (capture == generation)
                format("remote", f);
        },
        Qt::QueuedConnection);
    connect(
        loop, &AudioSource::pcm, this,
        [this, capture](QByteArray b) {
            if (capture == generation)
                receive("remote", b);
        },
        Qt::QueuedConnection);
    connect(
        loop, &AudioSource::sourceError, this,
        [this, capture](QString e) {
            if (capture == generation)
                emit channelError("remote", e);
        },
        Qt::QueuedConnection);
    loop->start();
    mic = microphoneFactory(micId);
    if (!mic) {
        emit channelError("microphone", QStringLiteral("未找到麦克风"));
        return;
    }
    mic->setParent(this);
    connect(
        mic, &AudioSource::formatReady, this,
        [this, capture](QAudioFormat f) {
            if (capture == generation)
                format("microphone", f);
        },
        Qt::QueuedConnection);
    connect(
        mic, &AudioSource::pcm, this,
        [this, capture](QByteArray b) {
            if (capture == generation)
                receive("microphone", b);
        },
        Qt::QueuedConnection);
    connect(
        mic, &AudioSource::sourceError, this,
        [this, capture](QString e) {
            if (capture == generation)
                emit channelError("microphone", e);
        },
        Qt::QueuedConnection);
    mic->start();
}
void AudioController::format(QString channel, QAudioFormat f) {
    if (!active)
        return;
    auto t = channel == "remote" ? remote : microphone;
    t->converter = std::make_unique<PcmConverter>(f);
    t->writer = std::make_unique<WavWriter>();
    auto id = newId();
    auto relative = "sessions/" + sid + "/" + channel + "-" + id + ".wav";
    QDir().mkpath(root + "/sessions/" + sid);
    if (!t->writer->open(root + "/" + relative)) {
        emit channelError(channel, QStringLiteral("录音文件创建失败"));
        stop();
        return;
    }
    t->record = {{"id", id},
                 {"sessionId", sid},
                 {"channel", channel},
                 {"path", relative},
                 {"startMs", offset + clock.elapsed()},
                 {"endMs", offset + clock.elapsed()}};
    emit segmentSaved(t->record);
}
void AudioController::receive(QString channel, const QByteArray &b) {
    if (!active)
        return;
    auto t = channel == "remote" ? remote : microphone;
    if (!t || !t->converter || !t->writer)
        return;
    auto pcm = t->converter->process(b);
    if (pcm.isEmpty())
        return;
    if (t->writer->size() + pcm.size() > 1024ll * 1024 * 1024) {
        t->record["endMs"] = t->record["startMs"].toInteger() + t->writer->size() / 48;
        t->writer->close();
        emit segmentSaved(t->record);
        auto id = newId();
        auto rel = "sessions/" + sid + "/" + channel + "-" + id + ".wav";
        t->record["id"] = id;
        t->record["path"] = rel;
        t->record["startMs"] = offset + clock.elapsed();
        if (!t->writer->open(root + "/" + rel)) {
            emit channelError(channel, QStringLiteral("录音分段失败"));
            stop();
            return;
        }
    }
    qint64 start = t->record["startMs"].toInteger() + t->writer->size() / 48;
    if (!t->writer->append(pcm)) {
        emit channelError(channel, QStringLiteral("录音写入失败，已停止采集"));
        stop();
        return;
    }
    double square = 0;
    for (int i = 0; i + 1 < pcm.size(); i += 2) {
        double v = qFromLittleEndian<qint16>(pcm.constData() + i) / 32768.;
        square += v * v;
    }
    emit levelChanged(channel, float(std::sqrt(square / (pcm.size() / 2))));
    emit pcmReady(channel, pcm, start);
}
void AudioController::stop() {
    ++generation;
    active = false;
    if (loop) {
        disconnect(loop, nullptr, this, nullptr);
        loop->requestInterruption();
        loop->quit();
        loop->wait();
        delete loop;
        loop = nullptr;
    }
    if (mic) {
        disconnect(mic, nullptr, this, nullptr);
        mic->requestInterruption();
        mic->quit();
        mic->wait();
        delete mic;
        mic = nullptr;
    }
    for (auto t : {remote, microphone})
        if (t && t->writer) {
            t->record["endMs"] = t->record["startMs"].toInteger() + t->writer->size() / 48;
            t->writer->close();
            emit segmentSaved(t->record);
            t->writer.reset();
        }
    remote.reset();
    microphone.reset();
}
