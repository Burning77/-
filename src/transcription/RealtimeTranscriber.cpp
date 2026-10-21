#include "RealtimeTranscriber.h"
#include "app/CredentialStore.h"
#include <QJsonDocument>
#include <QTimer>
#include <QtEndian>
#include <cmath>
void RealtimeTranscriber::connectSession(QString s, QString ch, const ProviderConfig &c) {
    stop();
    sid = s;
    channel = ch;
    config = c;
    closing = false;
    retries = 0;
    assembler = TranscriptAssembler();
    ranges.clear();
    turns.clear();
    connectSocket();
}
bool RealtimeTranscriber::send(const QJsonObject &o) {
    return transport && transport->send(QJsonDocument(o).toJson(QJsonDocument::Compact));
}
void RealtimeTranscriber::connectSocket() {
    if (closing)
        return;
    auto key = CredentialStore::read(config.credentialId());
    QUrl url(config.baseUrl);
    if (key.isEmpty() || url.scheme() != "wss" || url.host().isEmpty() || key.contains('\n') ||
        key.contains('\r')) {
        emit status(channel, QStringLiteral("转写未启动：请配置 WSS 地址与独立 API Key"));
        return;
    }
    auto ws = new WebSocketTransport(url, key, this);
    transport = ws;
    connect(ws, &WebSocketTransport::connected, this, [this, ws] {
        if (ws != transport || closing)
            return;
        QJsonObject transcription{{"model", config.model}};
        if (config.model == "gpt-live-transcribe") {
            transcription["languages"] = QJsonArray{"zh"};
            transcription["delay"] = "low";
        } else
            transcription["language"] = "zh";
        send({{"type", "session.update"},
              {"session",
               QJsonObject{
                   {"type", "transcription"},
                   {"audio", QJsonObject{{"input", QJsonObject{{"format", QJsonObject{{"type", "audio/pcm"},
                                                                                      {"rate", 24000}}},
                                                               {"transcription", transcription},
                                                               {"turn_detection", QJsonValue::Null}}}}}}}});
        emit status(channel, QStringLiteral("已连接，等待转写配置确认"));
    });
    connect(ws, &WebSocketTransport::message, this, [this, ws](QByteArray b) {
        if (ws == transport && !closing)
            event(b);
        else if (ws == transport && closing)
            event(b);
    });
    connect(ws, &WebSocketTransport::transportError, this, [this, ws](QString e) {
        if (ws != transport)
            return;
        ready = false;
        emit status(channel, e);
        if (sentBytes > 0)
            emit gap(channel, turnStart, endMs);
        sentBytes = 0;
        voice = false;
        if (!closing && retries < 5) {
            int delay = qMin(1000 * (1 << retries), 10000);
            retries++;
            QTimer::singleShot(delay, this, [this, ws] {
                if ((!transport || transport == ws) && !closing)
                    connectSocket();
            });
        }
    });
    connect(ws, &QThread::finished, ws, &QObject::deleteLater);
    connect(ws, &QObject::destroyed, this, [this, ws] {
        if (transport == ws)
            transport = nullptr;
    });
    ws->start();
}
void RealtimeTranscriber::appendAudio(const QByteArray &pcm, qint64 start) {
    if (closing || pcm.isEmpty())
        return;
    if (pending.isEmpty())
        pendingStart = start;
    pending += pcm;
    endMs = start + pcm.size() / 48;
    if (pending.size() > 30 * 48000) {
        auto dropped = pending.size() - 30 * 48000;
        dropped -= dropped % 2;
        emit gap(channel, pendingStart, pendingStart + dropped / 48);
        pending.remove(0, dropped);
        pendingStart += dropped / 48;
    }
    if (ready && pending.size() >= 4800)
        flush();
}
void RealtimeTranscriber::flush() {
    if (!ready || pending.isEmpty())
        return;
    // Keep a bounded turn even if buffered audio arrives after reconnection.
    while (ready && pending.size() >= 4800) {
        auto bytes = pending.left(4800);
        double rms = 0;
        for (int i = 0; i < bytes.size(); i += 2) {
            double v = qFromLittleEndian<qint16>(bytes.constData() + i) / 32768.;
            rms += v * v;
        }
        rms = std::sqrt(rms / (bytes.size() / 2));
        if (!sentBytes)
            turnStart = pendingStart;
        if (!send(
                {{"type", "input_audio_buffer.append"}, {"audio", QString::fromLatin1(bytes.toBase64())}})) {
            ready = false;
            emit status(channel, QStringLiteral("音频发送失败"));
            return;
        }
        pending.remove(0, 4800);
        pendingStart += 100;
        sentBytes += 4800;
        if (rms > 0.012) {
            voice = true;
            lastVoice = pendingStart;
        }
        if ((voice && pendingStart - lastVoice >= 800) || sentBytes >= 8 * 48000)
            commit();
    }
}
void RealtimeTranscriber::commit() {
    if (sentBytes < 4800)
        return;
    if (send({{"type", "input_audio_buffer.commit"}})) {
        turns.enqueue({turnStart, turnStart + sentBytes / 48});
        sentBytes = 0;
        voice = false;
    }
}
void RealtimeTranscriber::event(const QByteArray &bytes) {
    auto doc = QJsonDocument::fromJson(bytes);
    if (!doc.isObject())
        return;
    auto e = doc.object();
    auto type = e["type"].toString();
    if (type == "session.updated" || type == "transcription_session.updated") {
        ready = true;
        retries = 0;
        emit status(channel, QStringLiteral("正在实时转写"));
        flush();
    }
    auto itemId = e["item_id"].toString();
    if (type == "input_audio_buffer.committed" && !turns.isEmpty()) {
        ranges[itemId] = turns.dequeue();
        if (!completedItems.contains(itemId))
            unfinishedItems.insert(itemId);
    }
    if (type == "conversation.item.input_audio_transcription.completed" ||
        type == "conversation.item.input_audio_transcription.failed") {
        unfinishedItems.remove(itemId);
        completedItems.insert(itemId);
    }
    if (type == "error") {
        emit status(channel, QStringLiteral("转写服务错误：") + e["error"].toObject()["code"].toString());
    }
    if (type == "conversation.item.input_audio_transcription.failed") {
        auto range = ranges.value(e["item_id"].toString(), {turnStart, endMs});
        emit gap(channel, range.first, range.second);
        emit status(channel, QStringLiteral("一段音频转写失败"));
    }
    auto range = ranges.value(e["item_id"].toString(), QPair<qint64, qint64>{turnStart, endMs});
    auto row = assembler.apply(e, sid, channel, range.first, range.second);
    if (!row.isEmpty())
        emit segmentChanged(row);
}
void RealtimeTranscriber::finish(bool closeInput) {
    if (closing)
        return;
    flush();
    if (ready && !pending.isEmpty()) {
        if (!sentBytes)
            turnStart = pendingStart;
        if (send({{"type", "input_audio_buffer.append"},
                  {"audio", QString::fromLatin1(pending.toBase64())}})) {
            sentBytes += pending.size();
            pending.clear();
        }
    }
    commit();
    closing = closeInput;
}
void RealtimeTranscriber::stop() {
    closing = true;
    ready = false;
    pending.clear();
    turns.clear();
    ranges.clear();
    unfinishedItems.clear();
    completedItems.clear();
    sentBytes = 0;
    voice = false;
    if (transport) {
        auto ws = transport;
        transport = nullptr;
        disconnect(ws, nullptr, this, nullptr);
        ws->stop();
        ws->wait();
        delete ws;
    }
}
