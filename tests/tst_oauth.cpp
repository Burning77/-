#include "auth/OAuthProtocol.h"
#include <QFile>
#include <QJsonDocument>
#include <QUrlQuery>
#include <QtTest>
class OAuthTest : public QObject {
    Q_OBJECT
  private slots:
    void pkceAndFreshAttempts() {
        QCOMPARE(OAuthProtocol::challenge("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk"),
                 QString("E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"));
        auto a = OAuthProtocol::create("dynamic_agent_client", 1234),
             b = OAuthProtocol::create("dynamic_agent_client", 1234);
        QVERIFY(a.verifier.size() >= 43);
        QVERIFY(a.state != b.state);
        QVERIFY(a.nonce != b.nonce);
        auto url = OAuthProtocol::authorization(a, "urn:uuid:fixture");
        QUrlQuery q(url);
        QCOMPARE(url.host(), QString("auth.openai.com"));
        QCOMPARE(q.queryItemValue("code_challenge_method"), QString("S256"));
        QCOMPARE(q.queryItemValue("redirect_uri"), QString("http://127.0.0.1:1234/auth/callback"));
        QVERIFY(q.queryItemValue("scope").contains("chatgpt.tokens.use.direct"));
    }
    void validatesCallback() {
        OAuthProtocol::Attempt a;
        a.state = "expected";
        a.clientId = "dynamic_agent_client";
        a.redirectUri = "http://127.0.0.1:1234/auth/callback";
        auto good = OAuthProtocol::callback(
            QUrl(a.redirectUri + "?state=expected&code=code1&client_id=oaiapp_fixture"), a);
        QCOMPARE(good.code, QString("code1"));
        QCOMPARE(good.clientId, QString("oaiapp_fixture"));
        for (auto suffix : {"?state=wrong&code=c&client_id=oaiapp_x",
                            "?state=expected&state=other&code=c&client_id=oaiapp_x",
                            "?state=expected&code=c&client_id=dynamic_agent_client",
                            "?state=expected&error=access_denied"}) {
            auto bad = OAuthProtocol::callback(QUrl(a.redirectUri + suffix), a);
            QVERIFY(!bad.error.isEmpty());
            QVERIFY(bad.code.isEmpty());
        }
        a.clientId = "oaiapp_original";
        QVERIFY(
            !OAuthProtocol::callback(QUrl(a.redirectUri + "?state=expected&code=c&client_id=oaiapp_other"), a)
                 .error.isEmpty());
        QVERIFY(!OAuthProtocol::callback(QUrl("http://evil.test/auth/callback?state=expected&code=c"), a)
                     .error.isEmpty());
    }
    void validatesSignatureAndClaims() {
        QFile f(QFINDTESTDATA("fixtures/oauth.json"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        auto fixture = QJsonDocument::fromJson(f.readAll()).object();
        auto token = fixture["token"].toString();
        auto jwks = fixture["jwks"].toObject();
        QString error;
        auto claims = OAuthProtocol::verifyIdToken(token, jwks, "oaiapp_fixture", "nonce1", 1000, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(claims["sub"].toString(), QString("fixture-user"));
        QVERIFY(OAuthProtocol::verifyIdToken(token, jwks, "wrong", "nonce1", 1000, &error).isEmpty());
        QVERIFY(OAuthProtocol::verifyIdToken(token, jwks, "oaiapp_fixture", "wrong", 1000, &error).isEmpty());
        QVERIFY(
            OAuthProtocol::verifyIdToken(token, jwks, "oaiapp_fixture", "nonce1", 3000, &error).isEmpty());
        auto parts = token.split('.');
        auto payload = QJsonDocument::fromJson(
                           QByteArray::fromBase64(parts[1].toLatin1(), QByteArray::Base64UrlEncoding))
                           .object();
        payload["sub"] = "attacker";
        parts[1] = QString::fromLatin1(
            QJsonDocument(payload)
                .toJson(QJsonDocument::Compact)
                .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
        QVERIFY(OAuthProtocol::verifyIdToken(parts.join('.'), jwks, "oaiapp_fixture", "nonce1", 1000, &error)
                    .isEmpty());
    }
};
QTEST_MAIN(OAuthTest)
#include "tst_oauth.moc"
