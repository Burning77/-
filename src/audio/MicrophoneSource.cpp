#include "MicrophoneSource.h"
#include <QAudioSource>
#include <QIODevice>
void MicrophoneSource::run() {
    auto format = device.preferredFormat();
    QAudioSource source(device, format);
    auto io = source.start();
    if (!io) {
        emit sourceError(QStringLiteral("麦克风启动失败，请检查权限和设备"));
        return;
    }
    emit formatReady(format);
    connect(io, &QIODevice::readyRead, &source, [this, io] {
        auto bytes = io->readAll();
        if (!bytes.isEmpty())
            emit pcm(bytes);
    });
    connect(&source, &QAudioSource::stateChanged, &source, [this, &source](QtAudio::State state) {
        if (state == QtAudio::StoppedState && source.error() != QtAudio::NoError)
            emit sourceError(QStringLiteral("麦克风设备中断"));
    });
    if (!isInterruptionRequested())
        exec();
    source.stop();
}
