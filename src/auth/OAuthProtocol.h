#pragma once
#include <QJsonObject>
#include <QUrl>
namespace OAuthProtocol {
struct Attempt {
    QString verifier, state, nonce, clientId, redirectUri;
};
struct Callback {
    QString code, clientId, error;
};
QString challenge(const QString &);
Attempt create(const QString &, quint16);
QUrl authorization(const Attempt &, const QString &);
Callback callback(const QUrl &, const Attempt &);
QJsonObject verifyIdToken(const QString &, const QJsonObject &, const QString &, const QString &, qint64,
                          QString *error);
} // namespace OAuthProtocol
