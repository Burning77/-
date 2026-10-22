#pragma once
#include <QThread>
#include <QMutex>
#include <QQueue>
#include <QWaitCondition>
#include <QJsonObject>
class LocalWorker : public QThread {
    Q_OBJECT
  public:
    LocalWorker(QString sid, QString channel, QString model, QString library);
    QPair<qint64, qint64> append(QByteArray pcm, qint64 start);
    void finish(bool closeInput);
    void abort();
    bool pending() const;
    qint64 buffered() const;
  signals:
    void text(QJsonObject row);
    void message(QString text);
    void failure(QString text);

  protected:
    void run() override;

  private:
    struct Packet {
        QByteArray pcm;
        qint64 start = 0;
        bool flush = false;
    };
    QString sid, channel, modelPath, libraryPath;
    mutable QMutex mutex;
    QWaitCondition changed;
    QQueue<Packet> packets;
    qint64 bytes = 0;
    bool closing = false, aborted = false, stopped = false, busy = false, speech = false;
};
