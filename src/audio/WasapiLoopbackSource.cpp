#include "WasapiLoopbackSource.h"
#include <QElapsedTimer>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
template <class T> struct ComRef {
    T *p = nullptr;
    ~ComRef() {
        if (p)
            p->Release();
    }
    T *operator->() {
        return p;
    }
    void **address() {
        return reinterpret_cast<void **>(&p);
    }
};
QList<RenderDevice> WasapiLoopbackSource::devices() {
    QList<RenderDevice> rows;
    auto hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    {
        ComRef<IMMDeviceEnumerator> e;
        if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                       __uuidof(IMMDeviceEnumerator), e.address()))) {
            ComRef<IMMDeviceCollection> collection;
            if (SUCCEEDED(e->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection.p))) {
                UINT n = 0;
                collection->GetCount(&n);
                for (UINT i = 0; i < n; i++) {
                    ComRef<IMMDevice> d;
                    if (FAILED(collection->Item(i, &d.p)))
                        continue;
                    LPWSTR id = nullptr;
                    if (FAILED(d->GetId(&id)))
                        continue;
                    QString name = QString::fromWCharArray(id);
                    ComRef<IPropertyStore> props;
                    if (SUCCEEDED(d->OpenPropertyStore(STGM_READ, &props.p))) {
                        PROPVARIANT v;
                        PropVariantInit(&v);
                        if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR)
                            name = QString::fromWCharArray(v.pwszVal);
                        PropVariantClear(&v);
                    }
                    rows.append({QString::fromWCharArray(id), name});
                    CoTaskMemFree(id);
                }
            }
        }
    }
    if (SUCCEEDED(hr))
        CoUninitialize();
    return rows;
}
void WasapiLoopbackSource::run() {
    auto init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(init)) {
        emit sourceError(QStringLiteral("音频线程初始化失败"));
        return;
    }
    auto work = [this] {
        ComRef<IMMDeviceEnumerator> e;
        ComRef<IMMDevice> d;
        ComRef<IAudioClient> client;
        ComRef<IAudioCaptureClient> capture;
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                    __uuidof(IMMDeviceEnumerator), e.address())) ||
            FAILED(deviceId.isEmpty() ? e->GetDefaultAudioEndpoint(eRender, eConsole, &d.p)
                                      : e->GetDevice(deviceId.toStdWString().c_str(), &d.p)) ||
            FAILED(d->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, client.address()))) {
            emit sourceError(QStringLiteral("无法打开播放音频设备"));
            return;
        }
        WAVEFORMATEX *mix = nullptr;
        if (FAILED(client->GetMixFormat(&mix))) {
            emit sourceError(QStringLiteral("无法读取音频格式"));
            return;
        }
        QAudioFormat f;
        f.setSampleRate(mix->nSamplesPerSec);
        f.setChannelCount(mix->nChannels);
        bool floating = mix->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
        static const GUID floatSubtype = {3, 0, 0x10, {0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71}};
        if (mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
            floating = IsEqualGUID(reinterpret_cast<WAVEFORMATEXTENSIBLE *>(mix)->SubFormat, floatSubtype);
        if (floating && mix->wBitsPerSample == 32)
            f.setSampleFormat(QAudioFormat::Float);
        else if (mix->wBitsPerSample == 16)
            f.setSampleFormat(QAudioFormat::Int16);
        else if (mix->wBitsPerSample == 32)
            f.setSampleFormat(QAudioFormat::Int32);
        else if (mix->wBitsPerSample == 8)
            f.setSampleFormat(QAudioFormat::UInt8);
        if (!f.isValid()) {
            CoTaskMemFree(mix);
            emit sourceError(QStringLiteral("播放设备的采样格式暂不支持"));
            return;
        }
        auto hr =
            client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 0, 0, mix, nullptr);
        int block = mix->nBlockAlign;
        CoTaskMemFree(mix);
        if (FAILED(hr) || FAILED(client->GetService(__uuidof(IAudioCaptureClient), capture.address())) ||
            FAILED(client->Start())) {
            emit sourceError(QStringLiteral("回环录音启动失败"));
            return;
        }
        emit formatReady(f);
        QElapsedTimer clock;
        clock.start();
        qint64 emittedFrames = 0;
        while (!isInterruptionRequested()) {
            UINT32 next = 0;
            if (FAILED(capture->GetNextPacketSize(&next))) {
                emit sourceError(QStringLiteral("播放设备已断开"));
                break;
            }
            while (next && !isInterruptionRequested()) {
                BYTE *data = nullptr;
                UINT32 frames = 0;
                DWORD flags = 0;
                hr = capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
                if (FAILED(hr)) {
                    emit sourceError(QStringLiteral("回环音频读取失败"));
                    next = 0;
                    break;
                }
                emit pcm(flags & AUDCLNT_BUFFERFLAGS_SILENT
                             ? QByteArray(int(frames) * block, 0)
                             : QByteArray(reinterpret_cast<char *>(data), int(frames) * block));
                emittedFrames += frames;
                capture->ReleaseBuffer(frames);
                if (FAILED(capture->GetNextPacketSize(&next))) {
                    next = 0;
                    break;
                }
            }
            // A render endpoint may stop delivering packets when all applications are silent.
            auto due = clock.elapsed() * f.sampleRate() / 1000;
            auto missing = due - emittedFrames;
            if (!next && missing > f.sampleRate() / 10) {
                int frames = int(qMin<qint64>(missing - f.sampleRate() / 20, f.sampleRate() / 10));
                emit pcm(QByteArray(frames * block, 0));
                emittedFrames += frames;
            }
            msleep(10);
        }
        client->Stop();
    };
    work();
    CoUninitialize();
}
