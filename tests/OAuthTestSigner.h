#pragma once
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <cstring>
// Ephemeral fixture key: never saved or used with a real account.
class OAuthTestSigner {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_KEY_HANDLE key = nullptr;
    static QByteArray encode(QByteArray value) {
        return value.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    }

  public:
    OAuthTestSigner() {
        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_RSA_ALGORITHM, nullptr, 0) >= 0 &&
            BCryptGenerateKeyPair(algorithm, &key, 2048, 0) >= 0)
            BCryptFinalizeKeyPair(key, 0);
    }
    ~OAuthTestSigner() {
        if (key)
            BCryptDestroyKey(key);
        if (algorithm)
            BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    QJsonObject jwks() const {
        ULONG size = 0;
        if (!key || BCryptExportKey(key, nullptr, BCRYPT_RSAPUBLIC_BLOB, nullptr, 0, &size, 0) < 0)
            return {};
        QByteArray blob(size, 0);
        if (BCryptExportKey(key, nullptr, BCRYPT_RSAPUBLIC_BLOB, reinterpret_cast<PUCHAR>(blob.data()), size,
                            &size, 0) < 0)
            return {};
        BCRYPT_RSAKEY_BLOB header;
        std::memcpy(&header, blob.constData(), sizeof(header));
        const auto offset = int(sizeof(header));
        QJsonObject jwk{
            {"kty", "RSA"},
            {"kid", "ephemeral-test"},
            {"alg", "RS256"},
            {"e", QString::fromLatin1(encode(blob.mid(offset, header.cbPublicExp)))},
            {"n", QString::fromLatin1(encode(blob.mid(offset + header.cbPublicExp, header.cbModulus)))}};
        return {{"keys", QJsonArray{jwk}}};
    }
    QString token(QString client, QString nonce) const {
        const QJsonObject header{{"alg", "RS256"}, {"kid", "ephemeral-test"}};
        const QJsonObject claims{{"iss", "https://auth.openai.com"},
                                 {"aud", client},
                                 {"sub", "fixture-user"},
                                 {"nonce", nonce},
                                 {"exp", 2000},
                                 {"iat", 900}};
        auto data = encode(QJsonDocument(header).toJson(QJsonDocument::Compact)) + '.' +
                    encode(QJsonDocument(claims).toJson(QJsonDocument::Compact));
        auto hash = QCryptographicHash::hash(data, QCryptographicHash::Sha256);
        BCRYPT_PKCS1_PADDING_INFO padding{BCRYPT_SHA256_ALGORITHM};
        ULONG size = 0;
        if (!key || BCryptSignHash(key, &padding, reinterpret_cast<PUCHAR>(hash.data()), hash.size(), nullptr,
                                   0, &size, BCRYPT_PAD_PKCS1) < 0)
            return {};
        QByteArray signature(size, 0);
        if (BCryptSignHash(key, &padding, reinterpret_cast<PUCHAR>(hash.data()), hash.size(),
                           reinterpret_cast<PUCHAR>(signature.data()), size, &size, BCRYPT_PAD_PKCS1) < 0)
            return {};
        return QString::fromLatin1(data + '.' + encode(signature));
    }
};
