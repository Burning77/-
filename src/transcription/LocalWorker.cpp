#include "LocalWorker.h"
#include "LocalTranscriber.h"
#include "audio/PcmConverter.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLibrary>
#include <memory>
#include <mutex>
namespace {
struct Engine {
    QLibrary library;
    void *model = nullptr;
    using ModelNew = void *(*)(const char *);
    using ModelFree = void (*)(void *);
    using RecNew = void *(*)(void *, float);
    using RecFree = void (*)(void *);
    using Accept = int (*)(void *, const char *, int);
    using Result = const char *(*)(void *);
    using Reset = void (*)(void *);
    ModelNew modelNew = nullptr;
    ModelFree modelFree = nullptr;
    RecNew recNew = nullptr;
    RecFree recFree = nullptr;
    Accept accept = nullptr;
    Result partial = nullptr, result = nullptr, final = nullptr;
    Reset reset = nullptr;
    ~Engine() {
        if (model && modelFree)
            modelFree(model);
    }
};
std::shared_ptr<Engine> engine(QString model, QString library, QString &error) {
    static std::mutex mutex;
    static QHash<QString, std::weak_ptr<Engine>> cache;
    std::lock_guard<std::mutex> lock(mutex);
    auto key = model + "|" + library;
    auto shared = cache.value(key).lock();
    if (shared)
        return shared;
    auto e = std::make_shared<Engine>();
    e->library.setFileName(library);
    if (!e->library.load()) {
        error = QStringLiteral("本地识别库加载失败，请使用包含 local-asr 的完整运行包");
        return {};
    }
    e->modelNew = reinterpret_cast<Engine::ModelNew>(e->library.resolve("vosk_model_new"));
    e->modelFree = reinterpret_cast<Engine::ModelFree>(e->library.resolve("vosk_model_free"));
    e->recNew = reinterpret_cast<Engine::RecNew>(e->library.resolve("vosk_recognizer_new"));
    e->recFree = reinterpret_cast<Engine::RecFree>(e->library.resolve("vosk_recognizer_free"));
    e->accept = reinterpret_cast<Engine::Accept>(e->library.resolve("vosk_recognizer_accept_waveform"));
    e->partial = reinterpret_cast<Engine::Result>(e->library.resolve("vosk_recognizer_partial_result"));
    e->result = reinterpret_cast<Engine::Result>(e->library.resolve("vosk_recognizer_result"));
    e->final = reinterpret_cast<Engine::Result>(e->library.resolve("vosk_recognizer_final_result"));
    e->reset = reinterpret_cast<Engine::Reset>(e->library.resolve("vosk_recognizer_reset"));
    if (!e->modelNew || !e->modelFree || !e->recNew || !e->recFree || !e->accept || !e->partial ||
        !e->result || !e->final || !e->reset) {
        error = QStringLiteral("本地识别库版本不匹配，初始化失败");
        return {};
    }
    if (auto log = reinterpret_cast<void (*)(int)>(e->library.resolve("vosk_set_log_level")))
        log(-1);
    if (!QFileInfo::exists(model + "/am/final.mdl")) {
        error = QStringLiteral("本地中文模型加载失败，请检查模型目录");
        return {};
    }
    // Vosk's Windows stdio expects the current ANSI code page, not UTF-8 paths.
    auto native = QDir::toNativeSeparators(model).toLocal8Bit();
    e->model = e->modelNew(native.constData());
    if (!e->model) {
        error = QStringLiteral("本地中文模型加载失败，请检查目录或路径编码");
        return {};
    }
    cache[key] = e;
    return e;
}
} // namespace
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
    auto shared = engine(modelPath, libraryPath, error);
    if (!shared) {
        finishState();
        emit message(error);
        return;
    }
    if (isInterruptionRequested()) {
        finishState();
        return;
    }
    auto recognizer = shared->recNew(shared->model, 16000.f);
    if (!recognizer) {
        finishState();
        emit message(QStringLiteral("本地识别器创建失败"));
        return;
    }
    emit message(QStringLiteral("本地中文识别已就绪，音频留在本机"));
    QAudioFormat format;
    format.setSampleRate(24000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);
    PcmConverter converter(format, 16000);
    LocalResultAssembler assembler;
    qint64 start = -1, end = 0, lastPartial = 0;
    auto result = [&](bool final) {
        const char *json = final ? shared->final(recognizer) : shared->partial(recognizer);
        auto o = QJsonDocument::fromJson(QByteArray(json)).object();
        auto row = assembler.apply(sid, channel, o[final ? "text" : "partial"].toString(), final,
                                   qMax<qint64>(0, start), end);
        if (!row.isEmpty())
            emit text(row);
        if (final) {
            shared->reset(recognizer);
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
            int accepted = shared->accept(recognizer, pcm.constData(), int(pcm.size()));
            {
                QMutexLocker lock(&mutex);
                speech = true;
            }
            if (accepted < 0) {
                emit message(QStringLiteral("本地音频解码失败"));
                break;
            }
            if (accepted || end - start >= 8000)
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
    shared->recFree(recognizer);
    finishState();
}
