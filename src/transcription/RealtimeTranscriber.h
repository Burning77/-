#pragma once
#include "TranscriptAssembler.h"
#include "TurnTracker.h"
#include "WebSocketTransport.h"
#include "domain/Types.h"
#include <QHash>
#include <QObject>
#include <QQueue>
#include <QSet>
class RealtimeTranscriber : public QObject {
    Q_OBJECT
  public:
    explicit RealtimeTranscriber(QObject *p = nullptr) : QObject(p) {}
    ~RealtimeTranscriber() {
        stop();
    }
    void connectSession(QString sessionId, QString channel, const ProviderConfig &);
    void appendAudio(const QByteArray &pcm, qint64 startMs);
    void finish(bool closeInput = true);
    void stop();
    bool hasUnfinishedAudio() const {
        return !pending.isEmpty() || sentBytes > 0 || tracker.pending();
    }
  signals:
    void segmentChanged(QJsonObject segment);
    void status(QString channel, QString message);
    void gap(QString channel, qint64 startMs, qint64 endMs);

  private:
    void connectSocket();
    void event(const QByteArray &);
    bool send(const QJsonObject &);
    void commit();
    void flush();
    QString sid, channel;
    ProviderConfig config;
    WebSocketTransport *transport = nullptr;
    TranscriptAssembler assembler;
    QByteArray pending;
    qint64 pendingStart = 0, turnStart = 0, endMs = 0, lastVoice = 0, sentBytes = 0;
    bool ready = false, closing = true, voice = false;
    int retries = 0;
    TurnTracker tracker;
};
