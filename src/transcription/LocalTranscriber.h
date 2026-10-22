#pragma once
#include <QObject>
#include <QJsonObject>
#include <QPointer>
class LocalWorker;
class LocalResultAssembler {
  public:
    QJsonObject apply(QString, QString, QString, bool, qint64, qint64);

  private:
    QJsonObject row;
};
class LocalTranscriber : public QObject {
    Q_OBJECT
  public:
    explicit LocalTranscriber(QObject *p = nullptr);
    ~LocalTranscriber();
    void connectSession(QString, QString, QString, QString = {});
    void appendAudio(const QByteArray &, qint64);
    void finish(bool = true);
    void stop();
    bool hasUnfinishedAudio() const;
    qint64 bufferedMilliseconds() const;
  signals:
    void segmentChanged(QJsonObject);
    void status(QString channel, QString message);
    void gap(QString channel, qint64 start, qint64 end);
    void failed(QString channel, QString reason);

  private:
    QPointer<LocalWorker> worker;
    quint64 generation = 0;
    QString channel;
    bool engineFailed = false;
    qint64 submittedStart = -1, submittedEnd = 0, finalizedEnd = 0;
};
