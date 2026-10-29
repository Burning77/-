#pragma once
#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QUrl>
#include <QUuid>
inline QString newId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
inline QString utcNow() {
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}
struct ProviderConfig {
    QString provider = "OpenAI", baseUrl = "https://api.openai.com/v1", model;
    QString accountId;
    bool accountLogin = false;
    QString reasoningEffort = "high";
    QString credentialId() const {
        return "InterviewAssistant/" +
               QString::fromLatin1(
                   QCryptographicHash::hash(
                       (provider + "|" + QUrl(baseUrl).adjusted(QUrl::StripTrailingSlash).toString())
                           .toUtf8(),
                       QCryptographicHash::Sha256)
                       .toHex());
    }
};
struct TextRequest {
    QString id = newId(), sessionId, questionId, operation;
    ProviderConfig config;
    QJsonArray messages;
    QByteArray imagePng;
    bool jsonMode = false;
};
