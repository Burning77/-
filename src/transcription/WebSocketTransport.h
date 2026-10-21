#pragma once
#include <QMutex>
#include <QQueue>
#include <QWaitCondition>
#include <QThread>
#include <QUrl>
#include <atomic>
class WebSocketTransport : public QThread {
    Q_OBJECT
  public:
    WebSocketTransport(QUrl url, QString key, QObject *p = nullptr) : QThread(p), url(url), key(key) {}
    ~WebSocketTransport() {
        stop();
        wait();
    }
    bool send(const QByteArray &bytes);
    void stop();
  signals:
    void connected();
    void message(QByteArray bytes);
    void transportError(QString message);

  protected:
    void run() override;

  private:
    QUrl url;
    QString key;
    std::atomic<void *> socket{nullptr}, request{nullptr};
    std::atomic_bool stopping{false};
    QMutex mutex;
    QWaitCondition outgoingReady;
    QQueue<QByteArray> outgoing;
    qint64 queuedBytes = 0;
};
