#include "PcmConverter.h"
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>
QByteArray PcmConverter::process(const QByteArray &bytes) {
    QByteArray out;
    int frameSize = format.bytesPerFrame();
    if (frameSize <= 0 || format.sampleRate() <= 0)
        return out;
    tail += bytes;
    int frames = tail.size() / frameSize;
    int width = format.bytesPerSample();
    for (int n = 0; n < frames; n++) {
        float mono = 0;
        for (int ch = 0; ch < format.channelCount(); ch++) {
            auto p = tail.constData() + n * frameSize + ch * width;
            float value = 0;
            switch (format.sampleFormat()) {
            case QAudioFormat::Float:
                std::memcpy(&value, p, 4);
                break;
            case QAudioFormat::Int16:
                value = qFromLittleEndian<qint16>(p) / 32768.f;
                break;
            case QAudioFormat::Int32:
                value = qFromLittleEndian<qint32>(p) / 2147483648.f;
                break;
            case QAudioFormat::UInt8:
                value = (static_cast<unsigned char>(*p) - 128) / 128.f;
                break;
            default:
                break;
            }
            mono += std::isfinite(value) ? value : 0;
        }
        samples.append(std::clamp(mono / format.channelCount(), -1.f, 1.f));
    }
    tail.remove(0, frames * frameSize);
    double step = double(format.sampleRate()) / 24000.;
    while (position + 1 < samples.size()) {
        int i = int(position);
        float value = samples[i] + float(position - i) * (samples[i + 1] - samples[i]);
        qint16 v = qint16(std::clamp(std::lround(value * 32768.f), -32768l, 32767l));
        char data[2];
        qToLittleEndian<qint16>(v, data);
        out.append(data, 2);
        position += step;
    }
    int used = std::min(int(position), int(samples.size()));
    if (used) {
        samples.remove(0, used);
        position -= used;
    }
    return out;
}
