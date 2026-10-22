#pragma once
#include "domain/Types.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QSet>
#include <QPointer>
class DeepSeekAccount;
#include <functional>
class TextClient : public QObject {
    Q_OBJECT
  public:
    explicit TextClient(QObject *parent = nullptr, DeepSeekAccount *deepSeek = nullptr);
    std::function<QString(const QString &)> keyReader;
    std::function<QNetworkReply *(const QNetworkRequest &, const QByteArray &)> postRequest;
    QString start(const TextRequest &);
    void cancel(const QString &);
    static QJsonObject payload(const TextRequest &);
  signals:
    void textDelta(QString requestId, QString text);
    void finished(QString requestId);
    void failed(QString requestId, QString error);

  private:
    QPointer<DeepSeekAccount> account;
    QNetworkAccessManager manager;
    QHash<QString, QNetworkReply *> replies;
    QHash<QString, QString> profileRequests;
    QSet<QString> pendingAuth;
    QString begin(const TextRequest &, const QString &key);
};
