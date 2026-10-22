#include "auth/ChatGptAccount.h"
#include "auth/SecretStore.h"
#include "OAuthTestSigner.h"
#include <QFile>
#include <QJsonDocument>
#include <QSettings>
#include <QTcpSocket>
#include <QUrlQuery>
#include <QtTest>
class ChatGptTest : public QObject {
    Q_OBJECT
    QTemporaryDir settings;
    QString client = "oaiapp_fixture";
    QString secretPath(QString root) {
        return root + "/" +
               QString::fromLatin1(
                   QCryptographicHash::hash(client.toUtf8(), QCryptographicHash::Sha256).toHex()) +
               ".dpapi";
    }
    QJsonObject fixture() {
        QFile f(QFINDTESTDATA("fixtures/oauth.json"));
        if (!f.open(QIODevice::ReadOnly))
            return {};
        return QJsonDocument::fromJson(f.readAll()).object();
    }
    QJsonObject credentials(qint64 expiry) {
        return {{"access_token", "old-access"},
                {"refresh_token", "old-refresh"},
                {"scope", "chatgpt.tokens.use.direct offline_access"},
                {"expiresAt", expiry}};
    }
  private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("InterviewAssistantTests");
        QCoreApplication::setApplicationName("ChatGptRegression");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    }
    void init() {
        QSettings s;
        s.clear();
        s.setValue("chatgpt/active", client);
        s.setValue("chatgpt/profiles",
                   QJsonDocument(QJsonArray{QJsonObject{
                                     {"clientId", client}, {"sub", "fixture-user"}, {"planEnabled", true}}})
                       .toJson(QJsonDocument::Compact));
    }
    void protectsSecretsAndReturnsCachedToken() {
        QTemporaryDir dir;
        auto p = secretPath(dir.path());
        QVERIFY(SecretStore::write(p, credentials(2000)));
        QFile file(p);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(!file.readAll().contains("old-refresh"));
        ChatGptAccount account(nullptr, dir.path());
        account.now = []() { return 1000; };
        int network = 0;
        account.transport = [&](auto, auto, auto, auto, auto) { network++; };
        QString token, error;
        account.token(client, [&](QString t, QString e) {
            token = t;
            error = e;
        });
        QCOMPARE(token, QString("old-access"));
        QVERIFY(error.isEmpty());
        QCOMPARE(network, 0);
    }
    void serializesRefreshAndDiscardsAfterAccountSwitch() {
        QTemporaryDir dir;
        QVERIFY(SecretStore::write(secretPath(dir.path()), credentials(500)));
        ChatGptAccount account(nullptr, dir.path());
        account.now = []() { return 1000; };
        ChatGptAccount::JsonCallback post;
        int requests = 0;
        account.transport = [&](QString method, QUrl, QByteArray, QString, ChatGptAccount::JsonCallback cb) {
            requests++;
            if (method == "POST")
                post = cb;
            else
                cb(fixture()["jwks"].toObject(), {});
        };
        int callbacks = 0;
        account.token(client, [&](QString, QString e) {
            QVERIFY(!e.isEmpty());
            callbacks++;
        });
        account.token(client, [&](QString, QString e) {
            QVERIFY(!e.isEmpty());
            callbacks++;
        });
        QCOMPARE(requests, 1);
        account.selectProfile({});
        QCOMPARE(callbacks, 2);
        QVERIFY(account.activeProfile().isEmpty());
        post({{"access_token", "new"},
              {"refresh_token", "new-refresh"},
              {"id_token", fixture()["token"]},
              {"expires_in", 3600},
              {"scope", "chatgpt.tokens.use.direct"},
              {"token_type", "Bearer"}},
             {});
        QCOMPARE(SecretStore::read(secretPath(dir.path()))["refresh_token"].toString(),
                 QString("old-refresh"));
        QCOMPARE(requests, 1);
    }
    void refreshRotatesCredentialsForAllWaiters() {
        QTemporaryDir dir;
        QVERIFY(SecretStore::write(secretPath(dir.path()), credentials(500)));
        ChatGptAccount account(nullptr, dir.path());
        account.now = []() { return 1000; };
        ChatGptAccount::JsonCallback post;
        int requests = 0;
        account.transport = [&](QString method, QUrl, QByteArray, QString, ChatGptAccount::JsonCallback cb) {
            requests++;
            if (method == "POST")
                post = cb;
            else
                cb(fixture()["jwks"].toObject(), {});
        };
        int done = 0;
        for (int i = 0; i < 2; ++i)
            account.token(client, [&](QString t, QString e) {
                QCOMPARE(t, QString("new-access"));
                QVERIFY(e.isEmpty());
                done++;
            });
        QCOMPARE(requests, 1);
        post({{"access_token", "new-access"},
              {"refresh_token", "new-refresh"},
              {"id_token", fixture()["token"]},
              {"expires_in", 3600},
              {"scope", "chatgpt.tokens.use.direct offline_access"},
              {"token_type", "Bearer"}},
             {});
        QCOMPARE(done, 2);
        QCOMPARE(requests, 2);
        QCOMPARE(SecretStore::read(secretPath(dir.path()))["refresh_token"].toString(),
                 QString("new-refresh"));
    }
    void logoutClearsSecretsAndRetainsRegistration() {
        QTemporaryDir dir;
        QVERIFY(SecretStore::write(secretPath(dir.path()), credentials(2000)));
        ChatGptAccount account(nullptr, dir.path());
        int revokes = 0;
        account.transport = [&](QString method, QUrl url, QByteArray body, QString,
                                ChatGptAccount::JsonCallback cb) {
            QCOMPARE(method, QString("POST"));
            QVERIFY(url.path().endsWith("/revoke"));
            QVERIFY(body.contains("client_id=oaiapp_fixture"));
            revokes++;
            cb({}, {});
        };
        account.signOut(client);
        QVERIFY(!QFile::exists(secretPath(dir.path())));
        QVERIFY(account.activeProfile().isEmpty());
        QCOMPARE(account.profiles().size(), 1);
        QCOMPARE(revokes, 1);
    }
    void logoutRetriesRevocationWithoutRestoringLocalSecrets() {
        QTemporaryDir dir;
        QVERIFY(SecretStore::write(secretPath(dir.path()), credentials(2000)));
        ChatGptAccount account(nullptr, dir.path());
        int revokes = 0;
        account.transport = [&](QString, QUrl, QByteArray, QString, ChatGptAccount::JsonCallback cb) {
            revokes++;
            cb({}, revokes < 3 ? QString("temporary network failure") : QString());
        };
        account.signOut(client);
        QVERIFY(!QFile::exists(secretPath(dir.path())));
        QTRY_COMPARE_WITH_TIMEOUT(revokes, 3, 4000);
        QVERIFY(!QFile::exists(secretPath(dir.path())));
    }
    void wrongStateNeverExchangesCode() {
        QTemporaryDir dir;
        ChatGptAccount account(nullptr, dir.path());
        QUrl auth;
        account.openBrowser = [&](QUrl url) {
            auth = url;
            return true;
        };
        int calls = 0;
        account.transport = [&](auto, auto, auto, auto, auto) { calls++; };
        account.login();
        QVERIFY(account.loginPending());
        QUrl callback(QUrlQuery(auth).queryItemValue("redirect_uri"));
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, callback.port());
        QTRY_VERIFY(socket.state() == QTcpSocket::ConnectedState);
        socket.write("GET /auth/callback?state=wrong&code=bad&client_id=oaiapp_fake HTTP/1.1\r\nHost: "
                     "127.0.0.1\r\n\r\n");
        socket.flush();
        QTRY_VERIFY(socket.bytesAvailable() > 0 || socket.state() == QTcpSocket::UnconnectedState);
        QCOMPARE(calls, 0);
        QVERIFY(account.loginPending());
        account.cancelLogin();
    }
    void browserCallbackValidatesAndActivatesIssuedRegistration() {
        QTemporaryDir dir;
        ChatGptAccount account(nullptr, dir.path());
        account.now = [] { return 1000; };
        OAuthTestSigner signer;
        QVERIFY(!signer.jwks().isEmpty());
        QUrl auth;
        account.openBrowser = [&](QUrl url) {
            auth = url;
            return true;
        };
        int exchanges = 0;
        account.transport = [&](QString method, QUrl url, QByteArray body, QString,
                                ChatGptAccount::JsonCallback cb) {
            if (method == "GET") {
                cb(signer.jwks(), {});
                return;
            }
            QCOMPARE(url.path(), QString("/api/accounts/oauth/token"));
            exchanges++;
            const QUrlQuery form(QString::fromUtf8(body)), authorization(auth);
            QCOMPARE(form.queryItemValue("client_id"), QString("oaiapp_new"));
            bool saved = false;
            for (auto p : account.profiles())
                saved |= p.toObject()["clientId"] == "oaiapp_new";
            QVERIFY(saved); // registration persisted BEFORE exchanging the code
            QCOMPARE(OAuthProtocol::challenge(form.queryItemValue("code_verifier")),
                     authorization.queryItemValue("code_challenge"));
            QCOMPARE(form.queryItemValue("redirect_uri"), authorization.queryItemValue("redirect_uri"));
            cb({{"access_token", "signed-access"},
                {"refresh_token", "signed-refresh"},
                {"id_token", signer.token("oaiapp_new", authorization.queryItemValue("nonce"))},
                {"expires_in", 3600},
                {"scope", "chatgpt.tokens.use.direct offline_access"},
                {"token_type", "Bearer"}},
               {});
        };
        account.login();
        const QUrlQuery query(auth);
        const QUrl redirect(query.queryItemValue("redirect_uri"));
        QTcpSocket first, replay;
        for (auto socket : {&first, &replay}) {
            socket->connectToHost(QHostAddress::LocalHost, redirect.port());
            QTRY_COMPARE(socket->state(), QTcpSocket::ConnectedState);
        }
        const auto callback = "GET /auth/callback?state=" + query.queryItemValue("state").toUtf8() +
                              "&code=test&client_id=oaiapp_new HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n";
        first.write(callback);
        first.flush();
        QTRY_COMPARE(account.activeProfile(), QString("oaiapp_new"));
        replay.write(callback);
        replay.flush();
        QTRY_COMPARE(replay.state(), QTcpSocket::UnconnectedState);
        QCOMPARE(exchanges, 1);
        QString token;
        account.token("oaiapp_new", [&](QString value, QString error) {
            QVERIFY(error.isEmpty());
            token = value;
        });
        QCOMPARE(token, QString("signed-access"));
    }
};
QTEST_MAIN(ChatGptTest)
#include "tst_chatgpt.moc"
