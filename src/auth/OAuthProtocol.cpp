#include "OAuthProtocol.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QUrlQuery>
#include <cstring>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#endif
namespace OAuthProtocol {
static QString randomValue() {
    QByteArray bytes(32, 0);
    for (int i = 0; i < 32; i += 4) {
        auto n = QRandomGenerator::system()->generate();
        memcpy(bytes.data() + i, &n, 4);
    }
    return QString::fromLatin1(
        bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}
QString challenge(const QString &verifier) {
    return QString::fromLatin1(QCryptographicHash::hash(verifier.toUtf8(), QCryptographicHash::Sha256)
                                   .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}
Attempt create(const QString &client, quint16 port) {
    return {randomValue(), randomValue(), randomValue(), client,
            QString("http://127.0.0.1:%1/auth/callback").arg(port)};
}
QUrl authorization(const Attempt &a, const QString &host) {
    QUrl url("https://auth.openai.com/api/accounts/authorize");
    QUrlQuery q;
    for (auto pair : QList<QPair<QString, QString>>{
             {"client_id", a.clientId},
             {"response_type", "code"},
             {"redirect_uri", a.redirectUri},
             {"scope", "openid profile email offline_access resource.invoke chatgpt.tokens.use.direct"},
             {"resource", "https://api.openai.com/v1"},
             {"state", a.state},
             {"nonce", a.nonce},
             {"code_challenge_method", "S256"},
             {"code_challenge", challenge(a.verifier)},
             {"ext_agent_host_id", host}})
        q.addQueryItem(pair.first, pair.second);
    if (a.clientId == "dynamic_agent_client")
        q.addQueryItem("agent_name_hint", "Qt Interview Assistant");
    url.setQuery(q);
    return url;
}
Callback callback(const QUrl &url, const Attempt &a) {
    auto fail = [](QString error) { return Callback{{}, {}, error}; };
    QUrl expected(a.redirectUri);
    if (url.scheme() != expected.scheme() || url.host() != expected.host() || url.port() != expected.port() ||
        url.path() != expected.path() || !url.userInfo().isEmpty() || !url.fragment().isEmpty())
        return fail(QStringLiteral("登录回调地址不匹配"));
    QUrlQuery q(url);
    QSet<QString> seen;
    for (auto item : q.queryItems(QUrl::FullyDecoded)) {
        if (seen.contains(item.first))
            return fail(QStringLiteral("登录回调参数重复"));
        seen.insert(item.first);
    }
    if (a.state.isEmpty() || q.queryItemValue("state", QUrl::FullyDecoded) != a.state)
        return fail(QStringLiteral("登录回调state不匹配"));
    if (q.hasQueryItem("error"))
        return fail(QStringLiteral("登录授权被取消或未获允许"));
    auto code = q.queryItemValue("code", QUrl::FullyDecoded),
         client = q.queryItemValue("client_id", QUrl::FullyDecoded);
    if (a.clientId != "dynamic_agent_client") {
        if (!client.isEmpty() && client != a.clientId)
            return fail(QStringLiteral("账户注册ID与登录请求不匹配"));
        client = a.clientId;
    }
    if (code.isEmpty() || code.size() > 8192 || client == "dynamic_agent_client" ||
        !QRegularExpression("^[A-Za-z0-9_-]{1,200}$").match(client).hasMatch())
        return fail(QStringLiteral("登录回调缺少有效授权码或注册ID"));
    return {code, client, {}};
}
QJsonObject verifyIdToken(const QString &token, const QJsonObject &jwks, const QString &client,
                          const QString &nonce, qint64 now, QString *error) {
    if (error)
        error->clear();
    auto fail = [&](QString why) {
        if (error)
            *error = why;
        return QJsonObject{};
    };
    auto parts = token.toLatin1().split('.');
    if (parts.size() != 3 || token.size() > 32768)
        return fail(QStringLiteral("身份令牌格式无效"));
    auto decode = [](QByteArray b) {
        return QByteArray::fromBase64(b, QByteArray::Base64UrlEncoding |
                                             QByteArray::AbortOnBase64DecodingErrors);
    };
    const auto header = QJsonDocument::fromJson(decode(parts[0])).object();
    const auto claims = QJsonDocument::fromJson(decode(parts[1])).object();
    if (header["alg"] != "RS256" || header["kid"].toString().isEmpty())
        return fail(QStringLiteral("身份令牌签名算法无效"));
    QJsonObject jwk;
    int matches = 0;
    for (auto v : jwks["keys"].toArray())
        if (v.toObject()["kid"] == header["kid"]) {
            jwk = v.toObject();
            matches++;
        }
    if (matches != 1 || jwk["kty"] != "RSA" ||
        (!jwk.value("alg").isUndefined() && jwk.value("alg") != "RS256") ||
        (!jwk.value("use").isUndefined() && jwk.value("use") != "sig"))
        return fail(QStringLiteral("身份签名公钥无效"));
    auto n = decode(jwk["n"].toString().toLatin1()), e = decode(jwk["e"].toString().toLatin1()),
         sig = decode(parts[2]);
    bool verified = false;
#ifdef Q_OS_WIN
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_KEY_HANDLE publicKey = nullptr;
    if (n.size() >= 256 && n.size() <= 1024 && !e.isEmpty() && e.size() <= 8 && sig.size() == n.size() &&
        BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_RSA_ALGORITHM, nullptr, 0) >= 0) {
        BCRYPT_RSAKEY_BLOB h{};
        h.Magic = BCRYPT_RSAPUBLIC_MAGIC;
        h.BitLength = n.size() * 8;
        h.cbPublicExp = e.size();
        h.cbModulus = n.size();
        QByteArray blob(reinterpret_cast<char *>(&h), sizeof(h));
        blob += e;
        blob += n;
        if (BCryptImportKeyPair(algorithm, nullptr, BCRYPT_RSAPUBLIC_BLOB, &publicKey,
                                reinterpret_cast<PUCHAR>(blob.data()), blob.size(), 0) >= 0) {
            auto hash = QCryptographicHash::hash(parts[0] + '.' + parts[1], QCryptographicHash::Sha256);
            BCRYPT_PKCS1_PADDING_INFO padding{BCRYPT_SHA256_ALGORITHM};
            verified = BCryptVerifySignature(publicKey, &padding, reinterpret_cast<PUCHAR>(hash.data()),
                                             hash.size(), reinterpret_cast<PUCHAR>(sig.data()), sig.size(),
                                             BCRYPT_PAD_PKCS1) >= 0;
        }
    }
    if (publicKey)
        BCryptDestroyKey(publicKey);
    if (algorithm)
        BCryptCloseAlgorithmProvider(algorithm, 0);
#endif
    if (!verified)
        return fail(QStringLiteral("身份令牌签名校验失败"));
    const auto aud = claims["aud"];
    bool audience = aud == client || (aud.isArray() && aud.toArray().contains(client));
    if (!audience || client.isEmpty() || claims["iss"] != "https://auth.openai.com" ||
        claims["sub"].toString().isEmpty() || !claims["exp"].isDouble() ||
        claims["exp"].toInteger() <= now - 30 || claims["iat"].toInteger(now) > now + 60 ||
        claims["nbf"].toInteger(now) > now + 60 || (!nonce.isEmpty() && claims["nonce"] != nonce) ||
        (!claims["azp"].isUndefined() && claims["azp"] != client) ||
        (aud.isArray() && aud.toArray().size() > 1 && claims["azp"] != client))
        return fail(QStringLiteral("身份令牌的账户、有效期或nonce校验失败"));
    return claims;
}
} // namespace OAuthProtocol
