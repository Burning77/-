#pragma once
#include "domain/Types.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <functional>
class TextClient : public QObject {
    Q_OBJECT
  public:
    explicit TextClient(QObject *parent = nullptr);
    std::function<QString(const QString &)> keyReader;
    QString start(const TextRequest &);
    void cancel(const QString &);
    static QJsonObject payload(const TextRequest &);
  signals:
    void textDelta(QString requestId, QString text);
    void finished(QString requestId);
    void failed(QString requestId, QString error);

  private:
    QNetworkAccessManager manager;
    QHash<QString, QNetworkReply *> replies;
};
