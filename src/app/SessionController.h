#pragma once
#include "ai/TextClient.h"
#include "audio/AudioController.h"
#include "storage/SessionStore.h"
#include "transcription/RealtimeTranscriber.h"
#include "transcription/LocalTranscriber.h"
#include <QElapsedTimer>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QTimer>
class SessionController : public QObject {
    Q_OBJECT
  public:
    explicit SessionController(QString dataRoot = {}, QObject *p = nullptr);
    ~SessionController();
    SessionStore store;
    TextClient client;
    AudioController audio;
    QString createSession(QString title, QString role, QString experience);
    QString addQuestion(QString sid, QString text, bool generate = true);
    QString answerQuestion(QString sid, QString questionId, const ProviderConfig &);
    QString review(QString sid);
    void cancel(QString sid);
    void startRecording(QString sid, QString remoteId, QByteArray microphoneId);
    void togglePause();
    void finish();
    void extractNow(QString sid);
    void editTranscript(QString sid, QString id, QString text);
    void editQuestion(QString sid, QString id, QString text);
    bool recording() const {
        return live;
    }
    bool paused() const {
        return pauseState;
    }
    bool stopping() const {
        return stoppingState;
    }
    QString liveSessionId() const {
        return liveSid;
    }
    bool autoExtract = true, autoAnswer = true;
  signals:
    void changed(QString sessionId);
    void status(QString message);
    void transcriptionStatus(QString channel, QString message);
    void stateChanged();

  private:
    struct Job {
        TextRequest request;
        QJsonObject record;
        QList<QJsonObject> sources;
    };
    QHash<QString, Job> jobs;
    QQueue<QString> answers;
    QString activeAnswer, activeExtraction, liveSid, remoteDevice;
    QByteArray micDevice;
    RealtimeTranscriber remoteTranscriber, micTranscriber;
    LocalTranscriber localRemote, localMic;
    bool usingLocalAsr = false;
    QHash<QString, QJsonObject> pendingRemote;
    QTimer debounce, deadline, gapFlushTimer, generationFlushTimer;
    QHash<QString, QJsonObject> gapRows;
    QSet<QString> dirtyGapChannels, dirtyJobs;
    QElapsedTimer clock;
    qint64 pauseStarted = 0;
    QStringList audioIssues;
    bool live = false, pauseState = false, stoppingState = false;
    QJsonObject get(QString kind, QString sid, QString id) const;
    bool persist(QString kind, const QJsonObject &);
    void pump();
    void complete(QString id, bool ok, QString error = {});
    void acceptSegment(QJsonObject);
    void extractPending();
    void recordGap(QString channel, qint64 start, qint64 end);
    void flushGaps();
    void flushGenerations();
    void finishNow();
};
