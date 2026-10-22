#pragma once
#include "OAuthProtocol.h"
#include <QNetworkAccessManager>
#include <QTcpServer>
#include <QTimer>
#include <QSet>
#include <functional>
class ChatGptAccount : public QObject {
    Q_OBJECT
  public:
    using TokenCallback = std::function<void(QString token, QString error)>;
    using JsonCallback = std::function<void(QJsonObject, QString)>;
    static ChatGptAccount &shared();
    explicit ChatGptAccount(QObject *parent = nullptr, QString vaultRoot = {});
    QJsonArray profiles() const;
    QString activeProfile() const;
    void selectProfile(QString clientId);
    void login(QString clientId = {});
    void cancelLogin();
    void signOut(QString clientId);
    void token(QString clientId, TokenCallback callback);
    void models(QString clientId, JsonCallback callback);
    bool loginPending() const {
        return listener.isListening();
    }
    std::function<bool(QUrl)> openBrowser;
    std::function<void(QString, QUrl, QByteArray, QString, JsonCallback)> transport;
    std::function<qint64()> now;
  signals:
    void changed();
    void status(QString text);
    void invalidated(QString clientId);

  private:
    QNetworkAccessManager network;
    QTcpServer listener;
    QTimer loginTimeout;
    OAuthProtocol::Attempt attempt;
    QString vault;
    quint64 loginSerial = 0;
    QHash<QString, quint64> epochs;
    QHash<QString, quint64> credentialEpochs;
    QSet<QString> refreshInFlight;
    QHash<QString, QList<TokenCallback>> refreshing;
    void request(QString method, QUrl url, QByteArray body, QString bearer, JsonCallback callback);
    void acceptCallback();
    void exchange(QString code, QString client, quint64 serial);
    void saveTokens(QString client, QJsonObject tokens, QJsonObject claims, bool refreshing,
                    JsonCallback done);
    QString path(QString client) const;
    QJsonObject profile(QString client) const;
    void updateProfile(QJsonObject value);
    void invalidate(QString client, bool discardRefresh = false);
    void revoke(QByteArray body, int attempt = 0);
};
