#include "BilingualEngine.h"
#include "third_party/sherpa-onnx/c-api.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLibrary>
#include <QMutex>
#include <QMutexLocker>
#include <QHash>
#include <QtEndian>
#include <QVector>
#include <mutex>
namespace {
struct Model {
    QLibrary runtime, library;
    QMutex mutex;
    const SherpaOnnxOnlineRecognizer *recognizer = nullptr;
#define ASR_FUNCTION(name) decltype(&SherpaOnnx##name) name = nullptr
    ASR_FUNCTION(GetVersionStr);
    ASR_FUNCTION(CreateOnlineRecognizer);
    ASR_FUNCTION(DestroyOnlineRecognizer);
    ASR_FUNCTION(CreateOnlineStream);
    ASR_FUNCTION(DestroyOnlineStream);
    ASR_FUNCTION(OnlineStreamAcceptWaveform);
    ASR_FUNCTION(IsOnlineStreamReady);
    ASR_FUNCTION(DecodeOnlineStream);
    ASR_FUNCTION(GetOnlineStreamResult);
    ASR_FUNCTION(DestroyOnlineRecognizerResult);
    ASR_FUNCTION(OnlineStreamInputFinished);
    ASR_FUNCTION(OnlineStreamIsEndpoint);
#undef ASR_FUNCTION
    ~Model() {
        if (recognizer)
            DestroyOnlineRecognizer(recognizer);
    }
};
std::shared_ptr<Model> load(const QString &path, const QString &library, QString &error) {
    static std::mutex mutex;
    static QHash<QString, std::weak_ptr<Model>> cache;
    std::lock_guard<std::mutex> lock(mutex);
    const auto key = path + "|" + library;
    if (auto model = cache.value(key).lock())
        return model;
    auto m = std::make_shared<Model>();
    const auto libraryFile = QFileInfo(library).absoluteFilePath();
    const auto runtimeFile = QFileInfo(libraryFile).dir().filePath("onnxruntime.dll");
    if (!QFileInfo::exists(libraryFile) || !QFileInfo::exists(runtimeFile)) {
        error = QStringLiteral("本地中英识别库加载失败，请保留完整 local-asr 目录");
        return {};
    }
    // Qt's toolchain can carry an older ONNX Runtime on PATH. Load our pinned
    // runtime first; loading the C API against that older DLL can crash in DllMain.
    m->runtime.setFileName(runtimeFile);
    struct OrtBase {
        const void *(*getApi)(quint32);
        const char *(*version)();
    };
    using OrtBaseFn = const OrtBase *(*)();
    if (!m->runtime.load()) {
        error = QStringLiteral("本地中英 ONNX 运行库加载失败，请保留完整 local-asr 目录");
        return {};
    }
    auto ortBase = reinterpret_cast<OrtBaseFn>(m->runtime.resolve("OrtGetApiBase"));
    if (!ortBase || QString::fromLatin1(ortBase()->version()) != "1.28.2") {
        error = QStringLiteral("本地 ONNX 运行库版本不匹配，需要 1.28.2");
        return {};
    }
    m->library.setFileName(libraryFile);
    if (!m->library.load()) {
        error = QStringLiteral("本地中英识别库加载失败，请使用包含 local-asr 的完整运行包");
        return {};
    }
#define LOAD_ASR(name)                                                                                       \
    m->name = reinterpret_cast<decltype(m->name)>(m->library.resolve("SherpaOnnx" #name));                   \
    if (!m->name) {                                                                                          \
        error = QStringLiteral("本地中英识别库版本不匹配，初始化失败");                                      \
        return {};                                                                                           \
    }
    LOAD_ASR(GetVersionStr)
    LOAD_ASR(CreateOnlineRecognizer)
    LOAD_ASR(DestroyOnlineRecognizer)
    LOAD_ASR(CreateOnlineStream)
    LOAD_ASR(DestroyOnlineStream)
    LOAD_ASR(OnlineStreamAcceptWaveform)
    LOAD_ASR(IsOnlineStreamReady)
    LOAD_ASR(DecodeOnlineStream)
    LOAD_ASR(GetOnlineStreamResult)
    LOAD_ASR(DestroyOnlineRecognizerResult)
    LOAD_ASR(OnlineStreamInputFinished)
    LOAD_ASR(OnlineStreamIsEndpoint)
#undef LOAD_ASR
    if (QString::fromLatin1(m->GetVersionStr()) != "1.13.8") {
        error = QStringLiteral("本地识别库版本不匹配，需要 sherpa-onnx 1.13.8");
        return {};
    }
    QDir dir(path);
    auto encoder = dir.filePath("encoder-epoch-99-avg-1.int8.onnx").toUtf8();
    auto decoder = dir.filePath("decoder-epoch-99-avg-1.onnx").toUtf8();
    auto joiner = dir.filePath("joiner-epoch-99-avg-1.int8.onnx").toUtf8();
    for (const auto &file : {encoder, decoder, joiner}) {
        if (!QFile::exists(QString::fromUtf8(file))) {
            error = QStringLiteral(
                "本地中英模型加载失败，请选择运行包内 local-asr/bilingual 目录；旧中文模型不适用");
            return {};
        }
    }
    QFile tokensFile(dir.filePath("tokens.txt"));
    if (!tokensFile.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("本地中英模型词表读取失败");
        return {};
    }
    auto tokens = tokensFile.readAll();
    SherpaOnnxOnlineRecognizerConfig config{};
    config.feat_config.sample_rate = 16000;
    config.feat_config.feature_dim = 80;
    config.model_config.transducer = {encoder.constData(), decoder.constData(), joiner.constData()};
    config.model_config.tokens_buf = tokens.constData();
    config.model_config.tokens_buf_size = int(tokens.size());
    config.model_config.num_threads = 2;
    config.model_config.provider = "cpu";
    config.decoding_method = "greedy_search";
    config.enable_endpoint = 1;
    config.rule1_min_trailing_silence = 2.4f;
    config.rule2_min_trailing_silence = 1.2f;
    config.rule3_min_utterance_length = 20.f;
    m->recognizer = m->CreateOnlineRecognizer(&config);
    if (!m->recognizer) {
        error = QStringLiteral("本地中英模型初始化失败，请检查模型文件及路径");
        return {};
    }
    cache[key] = m;
    return m;
}
} // namespace
struct BilingualEngine::State {
    std::shared_ptr<Model> model;
    const SherpaOnnxOnlineStream *stream = nullptr;
    ~State() {
        if (stream) {
            QMutexLocker lock(&model->mutex);
            model->DestroyOnlineStream(stream);
        }
    }
    void decode(const std::function<bool()> &cancelled) {
        while (!cancelled() && model->IsOnlineStreamReady(model->recognizer, stream))
            model->DecodeOnlineStream(model->recognizer, stream);
    }
};
BilingualEngine::BilingualEngine() : state(std::make_unique<State>()) {}
BilingualEngine::~BilingualEngine() = default;
bool BilingualEngine::open(const QString &path, const QString &library, QString &error) {
    state->model = load(path, library, error);
    if (!state->model)
        return false;
    QMutexLocker lock(&state->model->mutex);
    state->stream = state->model->CreateOnlineStream(state->model->recognizer);
    if (!state->stream) {
        error = QStringLiteral("本地中英识别器创建失败");
        return false;
    }
    return true;
}
bool BilingualEngine::accept(const QByteArray &pcm16, const std::function<bool()> &cancelled) {
    QVector<float> samples(pcm16.size() / 2);
    for (qsizetype i = 0; i < samples.size(); ++i)
        samples[i] = qFromLittleEndian<qint16>(pcm16.constData() + i * 2) / 32768.f;
    auto &m = *state->model;
    QMutexLocker lock(&m.mutex);
    m.OnlineStreamAcceptWaveform(state->stream, 16000, samples.constData(), int(samples.size()));
    state->decode(cancelled);
    return m.OnlineStreamIsEndpoint(m.recognizer, state->stream);
}
QString BilingualEngine::result(bool final, const std::function<bool()> &cancelled) {
    auto &m = *state->model;
    QMutexLocker lock(&m.mutex);
    if (final) {
        // Finish the remaining encoder chunk, including the word at pause/end-of-recording.
        float tail[4800]{};
        m.OnlineStreamAcceptWaveform(state->stream, 16000, tail, 4800);
        m.OnlineStreamInputFinished(state->stream);
        state->decode(cancelled);
    }
    const auto *r = m.GetOnlineStreamResult(m.recognizer, state->stream);
    auto text = r ? QString::fromUtf8(r->text) : QString();
    if (r)
        m.DestroyOnlineRecognizerResult(r);
    if (final) {
        m.DestroyOnlineStream(state->stream);
        state->stream = m.CreateOnlineStream(m.recognizer);
    }
    return text;
}
