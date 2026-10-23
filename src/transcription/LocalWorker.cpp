#include "LocalWorker.h"
#include "LocalTranscriber.h"
#include "BilingualEngine.h"
#include "audio/PcmConverter.h"
LocalWorker::LocalWorker(QString s, QString c, QString m, QString l)
    : sid(s), channel(c), modelPath(m), libraryPath(l) {}
QPair<qint64, qint64> LocalWorker::append(QByteArray pcm, qint64 start) {
    QMutexLocker lock(&mutex);
    if (closing || aborted || stopped || pcm.isEmpty())
        return {-1, -1};
    // Small packets bound decoder work and make cancellation prompt.
    for (qsizetype offset = 0; offset < pcm.size(); offset += 4800) {
        auto part = pcm.mid(offset, 4800);
        packets.enqueue({part, start + offset / 48, false});
        bytes += part.size();
    }
    QPair<qint64, qint64> lost{-1, -1};
    while (bytes > 30 * 48000 && !packets.isEmpty()) {
        auto p = packets.dequeue();
        bytes -= p.pcm.size();
        if (!p.pcm.isEmpty()) {
            if (lost.first < 0)
                lost.first = p.start;
            lost.second = p.start + p.pcm.size() / 48;
        }
    }
    changed.wakeOne();
    return lost;
}
void LocalWorker::finish(bool closeInput) {
    QMutexLocker lock(&mutex);
    if (stopped || aborted || closing)
        return;
    packets.enqueue({{}, 0, true});
    closing = closeInput;
    changed.wakeOne();
}
void LocalWorker::abort() {
    QMutexLocker lock(&mutex);
    aborted = true;
    packets.clear();
    bytes = 0;
    changed.wakeOne();
    requestInterruption();
}
bool LocalWorker::pending() const {
    QMutexLocker lock(&mutex);
    return !stopped && (!packets.isEmpty() || busy || speech);
}
qint64 LocalWorker::buffered() const {
    QMutexLocker lock(&mutex);
    return bytes;
}
void LocalWorker::run() {
    auto finishState = [this] {
        QMutexLocker lock(&mutex);
        stopped = true;
        busy = false;
        speech = false;
        packets.clear();
        bytes = 0;
    };
    QString error;
    BilingualEngine decoder;
    emit message(QStringLiteral("正在加载本地中英双语模型…"));
    if (!decoder.open(modelPath, libraryPath, error)) {
        finishState();
        emit message(error);
        emit failure(error);
        return;
    }
    if (isInterruptionRequested()) {
        finishState();
        return;
    }
    auto cancelled = [this] { return isInterruptionRequested(); };
    emit message(QStringLiteral("本地中英混合识别已就绪，英文保留原文；音频留在本机"));
    QAudioFormat format;
    format.setSampleRate(24000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);
    PcmConverter converter(format, 16000);
    LocalResultAssembler assembler;
    qint64 start = -1, end = 0, lastPartial = 0;
    auto result = [&](bool final) {
        auto row = assembler.apply(sid, channel, decoder.result(final, cancelled), final,
                                   qMax<qint64>(0, start), end);
        if (!row.isEmpty())
            emit text(row);
        if (final) {
            start = -1;
            QMutexLocker lock(&mutex);
            speech = false;
        }
    };
    while (true) {
        Packet packet;
        {
            QMutexLocker lock(&mutex);
            while (packets.isEmpty() && !closing && !aborted)
                changed.wait(&mutex);
            if (aborted)
                break;
            if (packets.isEmpty() && closing)
                break;
            packet = packets.dequeue();
            bytes -= packet.pcm.size();
            busy = true;
        }
        if (packet.flush) {
            if (start >= 0)
                result(true);
            converter = PcmConverter(format, 16000);
        } else {
            if (start >= 0 && packet.start > end + 30) {
                result(true);
                converter = PcmConverter(format, 16000);
            }
            if (start < 0)
                start = packet.start;
            end = packet.start + packet.pcm.size() / 48;
            auto pcm = converter.process(packet.pcm);
            bool accepted = decoder.accept(pcm, cancelled);
            {
                QMutexLocker lock(&mutex);
                speech = true;
            }
            if (accepted || end - start >= 20000)
                result(true);
            else if (end - lastPartial >= 200) {
                result(false);
                lastPartial = end;
            }
        }
        {
            QMutexLocker lock(&mutex);
            busy = false;
        }
    }
    bool abortedNow;
    {
        QMutexLocker lock(&mutex);
        abortedNow = aborted;
    }
    if (!abortedNow && start >= 0)
        result(true);
    finishState();
}
