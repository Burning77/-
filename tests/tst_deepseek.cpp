#include "auth/DeepSeekAccount.h"
#include "auth/SecretStore.h"
#include "ai/TextClient.h"
#include "ui/SettingsDialog.h"
#include <QLabel>
#include <QPushButton>
#include <QNetworkProxy>
#include <QNetworkProxyFactory>
#include <QScopeGuard>
#include <QSslSocket>
#include <QSslError>
#include <QStandardPaths>
#include <QTcpServer>
#include <QFile>
#include <QSignalSpy>
#include <cstring>
#include <QSettings>
#include <QTemporaryDir>
#include <QTcpSocket>
#include <QUrlQuery>
#include <QUuid>
#include <QtTest>
class FixtureReply : public QNetworkReply {
    QByteArray body;
    qint64 offset = 0;
    bool ended = false;

  public:
    FixtureReply(QNetworkRequest request, QByteArray bytes, int status, QObject *parent,
                 QNetworkReply::NetworkError transportError = QNetworkReply::NoError, int phase = 0)
        : QNetworkReply(parent), body(bytes) {
        setRequest(request);
        setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        if (transportError != QNetworkReply::NoError)
            setError(transportError, "Fixture connection closed");
        else if (status != 200)
            setError(QNetworkReply::ContentAccessDenied, "Fixture error");
        QTimer::singleShot(0, this, [this, phase] {
            if (ended)
                return;
            if (phase & 1)
                emit socketStartedConnecting();
            if (phase & 2)
                emit encrypted();
            if (phase & 4)
                emit requestSent();
            if (phase & 8)
                emit sslErrors({QSslError(QSslError::CertificateUntrusted)});
            emit readyRead();
            ended = true;
            setFinished(true);
            emit finished();
        });
    }
    qint64 bytesAvailable() const override {
        return body.size() - offset + QNetworkReply::bytesAvailable();
    }
    void abort() override {
        if (ended)
            return;
        ended = true;
        setError(OperationCanceledError, "Cancelled");
        setFinished(true);
        emit finished();
    }
    qint64 readData(char *data, qint64 max) override {
        auto count = qMin(max, qint64(body.size()) - offset);
        if (!count)
            return -1;
        std::memcpy(data, body.constData() + offset, count);
        offset += count;
        return count;
    }
};
class DeepSeekTest : public QObject {
    Q_OBJECT
    QTemporaryDir settings;
    static QJsonObject envelope(QJsonObject value) {
        return {{"code", 0}, {"data", QJsonObject{{"biz_code", 0}, {"biz_data", value}}}};
    }
    void signIn(DeepSeekAccount &account) {
        auto init = std::make_shared<QJsonObject>();
        account.openBrowser = [](QUrl) { return true; };
        account.transport = [init](QString, QUrl url, QJsonObject body, QString,
                                   DeepSeekAccount::JsonCallback done) {
            if (url.path().endsWith("auth_init")) {
                *init = body;
                done(envelope({{"authorize_id", "fixture"},
                               {"authorize_url", "https://platform.deepseek.com/dsh/authorize"},
                               {"expires_in", 120}}),
                     {});
            } else if (url.path().endsWith("auth_exchange"))
                done(envelope({{"token", "fixture-token"},
                               {"authorized_url", "https://platform.deepseek.com/dsh/authorized"}}),
                     {});
            else
                done(envelope({}), {});
        };
        account.login();
        auto callback = QUrl((*init)["redirect_uri"].toString());
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, callback.port());
        QTRY_COMPARE(socket.state(), QTcpSocket::ConnectedState);
        socket.write("GET /oauth/callback?code=fixture&state=" + (*init)["state"].toString().toUtf8() +
                     " HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");
        socket.flush();
        QTRY_VERIFY(account.signedIn());
    }
  private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("InterviewAssistantTests");
        QCoreApplication::setApplicationName("DeepSeekRegression");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    }
    void init() {
        QSettings().clear();
    }
    void usesBundledOpenSslForAccountConnections() {
        QVERIFY(QSslSocket::setActiveBackend("schannel"));
        QCOMPARE(QSslSocket::activeBackend(), QString("schannel"));
        QTemporaryDir dir;
        DeepSeekAccount account(nullptr, dir.path());
        QCOMPARE(QSslSocket::activeBackend(), QString("openssl"));
        QVERIFY(QSslSocket::supportsSsl());
    }
    void catalogueFailureDoesNotHideSuccessfulLogin() {
        QStandardPaths::setTestModeEnabled(true);
        auto &account = DeepSeekAccount::shared();
        signIn(account);
        auto cleanup = qScopeGuard([&] {
            account.signOut();
            QStandardPaths::setTestModeEnabled(false);
        });
        account.transport = [](QString, QUrl url, QJsonObject, QString, DeepSeekAccount::JsonCallback done) {
            if (url.path() == "/models")
                done({}, "fixture-model-list-failed");
            else
                done(envelope({}), {});
        };
        // Placeholder checks use Windows Credential Manager, independent of test-mode
        // paths. Give all three API-key fields isolated identities before creating UI.
        const auto fixtureUrl = "https://" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".invalid";
        QSettings().setValue("DeepSeek/accountLogin", false);
        for (const auto &key : {"OpenAI/url", "DeepSeek/url", "transcription/url"})
            QSettings().setValue(key, fixtureUrl);
        SettingsDialog dialog;
        QPushButton *refresh = nullptr;
        for (auto button : dialog.findChildren<QPushButton *>())
            if (button->text() == QStringLiteral("读取账号可用模型"))
                refresh = button;
        QVERIFY(refresh);
        refresh->click();
        QLabel *status = nullptr;
        for (auto label : dialog.findChildren<QLabel *>())
            if (label->text().contains("fixture-model-list-failed"))
                status = label;
        QVERIFY(status);
        QVERIFY(account.signedIn());
        QVERIFY2(status->text().contains(QStringLiteral("账号已登录")), qPrintable(status->text()));
    }
    void catalogueNetworkFailureReportsActualCauseWithoutHttpZero() {
        QTemporaryDir dir;
        DeepSeekAccount account(nullptr, dir.path());
        signIn(account);
        account.transport = {};
        QTcpServer proxy;
        QVERIFY(proxy.listen(QHostAddress::LocalHost, 0));
        connect(&proxy, &QTcpServer::newConnection, &proxy, [&] {
            while (proxy.hasPendingConnections()) {
                auto socket = proxy.nextPendingConnection();
                connect(socket, &QTcpSocket::readyRead, socket, [socket] {
                    socket->readAll();
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
        const auto oldProxy = QNetworkProxy::applicationProxy();
        const auto oldSystem = QNetworkProxyFactory::usesSystemConfiguration();
        auto restore = qScopeGuard([&] {
            QNetworkProxy::setApplicationProxy(oldProxy);
            if (oldSystem)
                QNetworkProxyFactory::setUseSystemConfiguration(true);
        });
        QNetworkProxy::setApplicationProxy(
            QNetworkProxy(QNetworkProxy::HttpProxy, "127.0.0.1", proxy.serverPort()));
        bool finished = false;
        QString error;
        account.models([&](QJsonObject, QString why) {
            error = why;
            finished = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(finished, 5000);
        QVERIFY(account.signedIn());
        QVERIFY2(!error.contains("HTTP 0"), qPrintable(error));
        QVERIFY(error.contains(QStringLiteral("网络")));
        QVERIFY(error.contains("api.deepseek.com"));
        QVERIFY(!error.contains("fixture-token"));
    }
    void catalogueRecoversAfterTwoClosedConnections() {
        QTemporaryDir dir;
        DeepSeekAccount account(nullptr, dir.path());
        signIn(account);
        account.transport = {};
        const auto session = account.sessionId();
        int calls = 0, completions = 0;
        bool correctRequest = true;
        account.sendRequest = [&](QString method, const QNetworkRequest &request, const QByteArray &body) {
            ++calls;
            correctRequest &= method == "GET" && body.isEmpty() &&
                              request.url() == QUrl("https://api.deepseek.com/models") &&
                              request.rawHeader("x-dsh-auth-token") == "fixture-token";
            return new FixtureReply(
                request, calls < 3 ? QByteArray() : QByteArray("{\"data\":[]}"), calls < 3 ? 0 : 200,
                &account, calls < 3 ? QNetworkReply::RemoteHostClosedError : QNetworkReply::NoError);
        };
        QString error;
        account.models([&](QJsonObject result, QString why) {
            ++completions;
            error = why;
            if (why.isEmpty())
                QVERIFY(result["data"].isArray());
        });
        QTRY_COMPARE_WITH_TIMEOUT(completions, 1, 3000);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(calls, 3);
        QVERIFY(correctRequest);
        QCOMPARE(account.sessionId(), session);
    }
    void catalogueRetriesStopAfterThreeAttempts() {
        QTemporaryDir dir;
        DeepSeekAccount account(nullptr, dir.path());
        signIn(account);
        account.transport = {};
        int calls = 0, completions = 0;
        account.sendRequest = [&](QString, const QNetworkRequest &request, const QByteArray &) {
            ++calls;
            return new FixtureReply(request, {}, 0, &account, QNetworkReply::RemoteHostClosedError);
        };
        QString error;
        account.models([&](QJsonObject, QString why) {
            ++completions;
            error = why;
        });
        QTRY_COMPARE_WITH_TIMEOUT(completions, 1, 3000);
        QCOMPARE(calls, 3);
        QVERIFY(error.contains(QStringLiteral("重试")));
        QVERIFY(account.signedIn());
    }
    void catalogueDoesNotRetryServerOrCertificateErrors_data() {
        QTest::addColumn<int>("http");
        QTest::addColumn<int>("networkError");
        QTest::newRow("unauthorized") << 401 << int(QNetworkReply::ContentAccessDenied);
        QTest::newRow("server-error") << 503 << int(QNetworkReply::InternalServerError);
        QTest::newRow("certificate") << 0 << int(QNetworkReply::SslHandshakeFailedError);
        QTest::newRow("cancelled") << 0 << int(QNetworkReply::OperationCanceledError);
    }
    void catalogueDoesNotRetryServerOrCertificateErrors() {
        QFETCH(int, http);
        QFETCH(int, networkError);
        QTemporaryDir dir;
        DeepSeekAccount account(nullptr, dir.path());
        signIn(account);
        account.transport = {};
        int calls = 0, completions = 0;
        account.sendRequest = [&](QString, const QNetworkRequest &request, const QByteArray &) {
            ++calls;
            return new FixtureReply(request, "{}", http, &account, QNetworkReply::NetworkError(networkError));
        };
        account.models([&](QJsonObject, QString error) {
            ++completions;
            QVERIFY(!error.isEmpty());
        });
        QTRY_COMPARE(completions, 1);
        QCOMPARE(calls, 1);
        QVERIFY(account.signedIn());
    }
    void cataloguePendingRetryCannotResendSignedOutToken() {
        QTemporaryDir dir;
        DeepSeekAccount account(nullptr, dir.path());
        signIn(account);
        account.transport = {};
        int calls = 0, completions = 0;
        account.sendRequest = [&](QString method, const QNetworkRequest &request, const QByteArray &) {
            if (method != "GET")
                return new FixtureReply(request, QJsonDocument(envelope({})).toJson(), 200, &account);
            ++calls;
            auto reply = new FixtureReply(request, {}, 0, &account, QNetworkReply::RemoteHostClosedError);
            QTimer::singleShot(0, &account, [&] { account.signOut(); });
            return reply;
        };
        QString error;
        account.models([&](QJsonObject, QString why) {
            ++completions;
            error = why;
        });
        QTRY_COMPARE_WITH_TIMEOUT(completions, 1, 3000);
        QVERIFY(error.contains(QStringLiteral("账号已更改")));
        QCOMPARE(calls, 1);
        QVERIFY(!account.signedIn());
    }
    void inferenceRecoversOnlyBeforeSendingRequest() {
        QTemporaryDir dir;
        DeepSeekAccount account(nullptr, dir.path());
        signIn(account);
        TextClient client(nullptr, &account);
        int calls = 0;
        client.postRequest = [&](const QNetworkRequest &request, const QByteArray &) {
            ++calls;
            const QByteArray stream =
                "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"end_turn\"}}\n\n"
                "data: {\"type\":\"message_stop\"}\n\n";
            return new FixtureReply(request, calls < 3 ? QByteArray() : stream, calls < 3 ? 0 : 200, &client,
                                    calls < 3 ? QNetworkReply::RemoteHostClosedError : QNetworkReply::NoError,
                                    calls < 3 ? 1 : 7);
        };
        QSignalSpy done(&client, &TextClient::finished), failed(&client, &TextClient::failed);
        TextRequest request;
        request.config = {"DeepSeek", "https://api.deepseek.com", "fixture-model", account.sessionId(), true};
        client.start(request);
        QTRY_VERIFY_WITH_TIMEOUT(!done.isEmpty() || !failed.isEmpty(), 3000);
        QCOMPARE(done.size(), 1);
        QCOMPARE(failed.size(), 0);
        QCOMPARE(calls, 3);
        QVERIFY(account.signedIn());
    }
    void inferenceNeverReplaysPossiblySentRequest_data() {
        QTest::addColumn<int>("phase");
        QTest::addColumn<int>("http");
        QTest::addColumn<QByteArray>("body");
        QTest::newRow("encrypted") << 3 << 0 << QByteArray();
        QTest::newRow("sent") << 5 << 0 << QByteArray();
        QTest::newRow("reused-or-unknown") << 0 << 0 << QByteArray();
        QTest::newRow("certificate-error") << 9 << 0 << QByteArray();
        QTest::newRow("partial-answer") << 7 << 200
                                        << QByteArray("data: "
                                                      "{\"type\":\"content_block_delta\",\"delta\":{\"type\":"
                                                      "\"text_delta\",\"text\":\"partial\"}}\n\n");
        QTest::newRow("unauthorized") << 7 << 401 << QByteArray("{}");
    }
    void inferenceNeverReplaysPossiblySentRequest() {
        QFETCH(int, phase);
        QFETCH(int, http);
        QFETCH(QByteArray, body);
        QTemporaryDir dir;
        DeepSeekAccount account(nullptr, dir.path());
        signIn(account);
        TextClient client(nullptr, &account);
        int calls = 0;
        client.postRequest = [&](const QNetworkRequest &request, const QByteArray &) {
            ++calls;
            return new FixtureReply(request, body, http, &client, QNetworkReply::RemoteHostClosedError,
                                    phase);
        };
        QSignalSpy failed(&client, &TextClient::failed);
        TextRequest request;
        request.config = {"DeepSeek", "https://api.deepseek.com", "fixture-model", account.sessionId(), true};
        client.start(request);
        QTRY_COMPARE(failed.size(), 1);
        QCOMPARE(calls, 1);
        QCOMPARE(account.signedIn(), http != 401);
    }
    void inferenceDoesNotReplayOriginWithEarlierTls() {
        QTemporaryDir dir;
        DeepSeekAccount account(nullptr, dir.path());
        signIn(account);
        TextClient client(nullptr, &account);
        int calls = 0;
        client.postRequest = [&](const QNetworkRequest &request, const QByteArray &) {
            ++calls;
            return new FixtureReply(request, {}, 0, &client, QNetworkReply::RemoteHostClosedError,
                                    calls == 1 ? 3 : 1);
        };
        QSignalSpy failed(&client, &TextClient::failed);
        TextRequest request;
        request.config = {"DeepSeek", "https://api.deepseek.com", "fixture-model", account.sessionId(), true};
        client.start(request);
        QTRY_COMPARE(failed.size(), 1);
        request.id = newId();
        client.start(request);
        QTRY_COMPARE(failed.size(), 2);
        QCOMPARE(calls, 2);
    }
    void inferenceRetryIsBounded() {
        QTemporaryDir dir;
        DeepSeekAccount account(nullptr, dir.path());
        signIn(account);
        TextClient client(nullptr, &account);
        int calls = 0;
        client.postRequest = [&](const QNetworkRequest &request, const QByteArray &) {
            ++calls;
            return new FixtureReply(request, {}, 0, &client, QNetworkReply::RemoteHostClosedError, 1);
        };
        QSignalSpy failed(&client, &TextClient::failed);
        TextRequest request;
        request.config = {"DeepSeek", "https://api.deepseek.com", "fixture-model", account.sessionId(), true};
        client.start(request);
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 3000);
        QCOMPARE(calls, 3);
        QVERIFY(failed.first()[1].toString().contains(QStringLiteral("重试")));
    }
    void inferenceWaitingRetryCanBeCancelled_data() {
        QTest::addColumn<bool>("signOut");
        QTest::newRow("cancel") << false;
        QTest::newRow("sign-out") << true;
    }
    void inferenceWaitingRetryCanBeCancelled() {
        QFETCH(bool, signOut);
        QTemporaryDir dir;
        DeepSeekAccount account(nullptr, dir.path());
        signIn(account);
        TextClient client(nullptr, &account);
        int calls = 0;
        bool firstFinished = false;
        client.postRequest = [&](const QNetworkRequest &request, const QByteArray &) {
            ++calls;
            auto reply = new FixtureReply(request, {}, 0, &client, QNetworkReply::RemoteHostClosedError, 1);
            connect(reply, &QNetworkReply::finished, &client, [&] { firstFinished = true; });
            return reply;
        };
        QSignalSpy failed(&client, &TextClient::failed);
        TextRequest request;
        request.config = {"DeepSeek", "https://api.deepseek.com", "fixture-model", account.sessionId(), true};
        client.start(request);
        QTRY_VERIFY(firstFinished);
        QCOMPARE(failed.size(), 0);
        if (signOut)
            account.signOut();
        else
            client.cancel(request.id);
        QTRY_COMPARE(failed.size(), 1);
        QTest::qWait(350);
        QCOMPARE(calls, 1);
        QCOMPARE(failed.size(), 1);
    }
    void signsInThroughPkceAndClearsWithoutRestoringLateResponses() {
        QTemporaryDir dir;
        DeepSeekAccount account(nullptr, dir.path());
        QUrl browser;
        QJsonObject init;
        DeepSeekAccount::JsonCallback exchange;
        int exchanges = 0, revokes = 0;
        account.openBrowser = [&](QUrl url) {
            browser = url;
            return true;
        };
        account.transport = [&](QString, QUrl url, QJsonObject body, QString token,
                                DeepSeekAccount::JsonCallback done) {
            if (url.path().endsWith("auth_init")) {
                init = body;
                done({{"code", 0},
                      {"data",
                       QJsonObject{
                           {"biz_code", 0},
                           {"biz_data", QJsonObject{{"authorize_id", "fixture"},
                                                    {"authorize_url",
                                                     "https://platform.deepseek.com/dsh/authorize?fixture=1"},
                                                    {"expires_in", 120}}}}}},
                     {});
            } else if (url.path().endsWith("auth_exchange")) {
                exchanges++;
                exchange = done;
                QCOMPARE(body["redirect_uri"], init["redirect_uri"]);
                QVERIFY(!body["code_verifier"].toString().isEmpty());
            } else if (url.path().endsWith("logout")) {
                revokes++;
                QCOMPARE(token, QString("fixture-token"));
                done({{"code", 0}, {"data", QJsonObject{{"biz_code", 0}, {"biz_data", QJsonObject{}}}}}, {});
            } else
                done({}, {});
        };
        account.login();
        QVERIFY(!browser.isEmpty());
        QCOMPARE(init["code_challenge_method"].toString(), QString("S256"));
        auto callback = QUrl(init["redirect_uri"].toString());
        QCOMPARE(callback.path(), QString("/oauth/callback"));
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, callback.port());
        QTRY_COMPARE(socket.state(), QTcpSocket::ConnectedState);
        socket.write("GET /oauth/callback?code=fixture-code&state=" + init["state"].toString().toUtf8() +
                     " HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");
        socket.flush();
        QTRY_COMPARE(exchanges, 1);
        exchange({{"code", 0},
                  {"data",
                   QJsonObject{{"biz_code", 0},
                               {"biz_data", QJsonObject{{"token", "fixture-token"},
                                                        {"authorized_url",
                                                         "https://platform.deepseek.com/dsh/authorized"}}}}}},
                 {});
        QVERIFY(account.signedIn());
        auto session = account.sessionId();
        QVERIFY(!session.isEmpty());
        QString resolved;
        account.token(session, [&](QString token, QString error) {
            QVERIFY(error.isEmpty());
            resolved = token;
        });
        QCOMPARE(resolved, QString("fixture-token"));
        account.signOut();
        QVERIFY(!account.signedIn());
        QCOMPARE(revokes, 1);
        exchange({{"code", 0},
                  {"data",
                   QJsonObject{{"biz_code", 0},
                               {"biz_data", QJsonObject{{"token", "fixture-token"},
                                                        {"authorized_url",
                                                         "https://platform.deepseek.com/dsh/authorized"}}}}}},
                 {});
        QVERIFY(!account.signedIn());
    }
    void encryptedGrantCatalogueFailureAndStaleInvalidation() {
        QTemporaryDir dir;
        DeepSeekAccount account(nullptr, dir.path());
        signIn(account);
        QVERIFY(account.signedIn());
        auto session = account.sessionId();
        QFile file(dir.path() + "/session.dpapi");
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(!file.readAll().contains("fixture-token"));
        file.close();
        QCOMPARE(SecretStore::read(dir.path() + "/session.dpapi")["issuer"].toString(),
                 QString("https://platform.deepseek.com"));
        QString error;
        account.token("stale", [&](QString token, QString why) {
            QVERIFY(token.isEmpty());
            error = why;
        });
        QVERIFY(!error.isEmpty());
        account.transport = [](QString method, QUrl url, QJsonObject body, QString token,
                               DeepSeekAccount::JsonCallback done) {
            QCOMPARE(method, QString("GET"));
            QCOMPARE(url.toString(), QString("https://api.deepseek.com/models"));
            QVERIFY(body.isEmpty());
            QCOMPARE(token, QString("fixture-token"));
            done({}, "HTTP 401");
        };
        account.models([&](QJsonObject, QString why) { error = why; });
        QCOMPARE(error, QString("HTTP 401"));
        QVERIFY(account.signedIn());
        account.rejectToken("stale");
        QVERIFY(account.signedIn());
        account.rejectToken(session);
        QVERIFY(!account.signedIn());
        DeepSeekAccount reloaded(nullptr, dir.path());
        QVERIFY(!reloaded.signedIn());
        auto headers = DeepSeekAccount::headers();
        QCOMPARE(headers.size(), 5);
        QVERIFY(headers.contains({"x-client-platform", "desktop-win"}));
    }
    void rejectsWrongStateBeforeExchanging() {
        QTemporaryDir dir;
        DeepSeekAccount account(nullptr, dir.path());
        QJsonObject init;
        int exchanges = 0;
        account.openBrowser = [](QUrl) { return true; };
        account.transport = [&](QString, QUrl url, QJsonObject body, QString,
                                DeepSeekAccount::JsonCallback done) {
            if (url.path().endsWith("auth_init")) {
                init = body;
                done(envelope({{"authorize_id", "fixture"},
                               {"authorize_url", "https://platform.deepseek.com/dsh/authorize"},
                               {"expires_in", 120}}),
                     {});
            } else if (url.path().endsWith("auth_exchange"))
                exchanges++;
            else
                done(envelope({}), {});
        };
        account.login();
        auto callback = QUrl(init["redirect_uri"].toString());
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, callback.port());
        QTRY_COMPARE(socket.state(), QTcpSocket::ConnectedState);
        socket.write("GET /oauth/callback?code=fixture&state=wrong HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");
        socket.flush();
        QTRY_VERIFY(socket.bytesAvailable() > 0 || socket.state() == QTcpSocket::UnconnectedState);
        QCOMPARE(exchanges, 0);
        QVERIFY(!account.signedIn());
    }
    void cancelBeforeInitDoesNotOpenOrRestore() {
        QTemporaryDir dir;
        DeepSeekAccount account(nullptr, dir.path());
        DeepSeekAccount::JsonCallback init;
        int opens = 0, cancels = 0;
        account.openBrowser = [&](QUrl) {
            opens++;
            return true;
        };
        account.transport = [&](QString, QUrl url, QJsonObject body, QString,
                                DeepSeekAccount::JsonCallback done) {
            if (url.path().endsWith("auth_init"))
                init = done;
            else if (url.path().endsWith("auth_cancel")) {
                cancels++;
                QCOMPARE(body["authorize_id"].toString(), QString("fixture"));
                QVERIFY(!body["code_verifier"].toString().isEmpty());
                done(envelope({}), {});
            }
        };
        account.login();
        account.cancelLogin();
        init(envelope({{"authorize_id", "fixture"},
                       {"authorize_url", "https://platform.deepseek.com/dsh/authorize"},
                       {"expires_in", 120}}),
             {});
        QCOMPARE(opens, 0);
        QCOMPARE(cancels, 1);
        QVERIFY(!account.signedIn());
    }
    void messagesWireAndTerminalHandling_data() {
        QTest::addColumn<QString>("reason");
        QTest::addColumn<bool>("terminal");
        QTest::addColumn<int>("http");
        QTest::addColumn<bool>("success");
        QTest::newRow("complete") << QString("end_turn") << true << 200 << true;
        QTest::newRow("truncated") << QString("end_turn") << false << 200 << false;
        QTest::newRow("max-tokens") << QString("max_tokens") << true << 200 << false;
        QTest::newRow("auth-expired") << QString() << false << 401 << false;
        QTest::newRow("permission-denied") << QString() << false << 403 << false;
    }
    void messagesWireAndTerminalHandling() {
        QFETCH(QString, reason);
        QFETCH(bool, terminal);
        QFETCH(int, http);
        QFETCH(bool, success);
        QTemporaryDir dir;
        DeepSeekAccount account(nullptr, dir.path());
        signIn(account);
        QVERIFY(account.signedIn());
        TextClient client(nullptr, &account);
        int reads = 0;
        client.keyReader = [&](QString) {
            reads++;
            return QString();
        };
        QNetworkRequest sent;
        QByteArray sentBody;
        client.postRequest = [&](const QNetworkRequest &request, const QByteArray &body) {
            sent = request;
            sentBody = body;
            QByteArray stream = "data: "
                                "{\"type\":\"content_block_delta\",\"delta\":{\"type\":\"text_delta\","
                                "\"text\":\"answer\"}}\n\n";
            stream += "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"" + reason.toUtf8() +
                      "\"}}\n\n";
            if (terminal)
                stream += "data: {\"type\":\"message_stop\"}\n\n";
            return static_cast<QNetworkReply *>(new FixtureReply(request, stream, http, &client));
        };
        QSignalSpy done(&client, &TextClient::finished), failed(&client, &TextClient::failed),
            delta(&client, &TextClient::textDelta);
        TextRequest request;
        request.config.provider = "DeepSeek";
        request.config.accountLogin = true;
        request.config.accountId = account.sessionId();
        request.config.baseUrl = "https://api.deepseek.com";
        request.config.model = "fixture-model";
        request.messages = {QJsonObject{{"role", "user"}, {"content", "fixture"}}};
        client.start(request);
        QTRY_VERIFY(!done.isEmpty() || !failed.isEmpty());
        QCOMPARE(done.size(), success ? 1 : 0);
        QCOMPARE(failed.size(), success ? 0 : 1);
        QCOMPARE(reads, 0);
        QCOMPARE(sent.url().toString(), QString("https://api.deepseek.com/anthropic/v1/messages"));
        QCOMPARE(sent.rawHeader("x-dsh-auth-token"), QByteArray("fixture-token"));
        QCOMPARE(sent.rawHeader("anthropic-version"), QByteArray("2023-06-01"));
        QVERIFY(!sent.hasRawHeader("Authorization"));
        QVERIFY(!sent.hasRawHeader("x-api-key"));
        QVERIFY(!sent.hasRawHeader("x-client-platform"));
        auto payload = QJsonDocument::fromJson(sentBody).object();
        QCOMPARE(payload["max_tokens"].toInt(), 4096);
        QVERIFY(!payload.contains("response_format"));
        if (success)
            QCOMPARE(delta.first()[1].toString(), QString("answer"));
        QCOMPARE(account.signedIn(), http != 401);
    }
    void rejectsCrossOriginBrowserDestination() {
        QTemporaryDir dir;
        DeepSeekAccount account(nullptr, dir.path());
        int opens = 0;
        account.openBrowser = [&](QUrl) {
            opens++;
            return true;
        };
        account.transport = [](QString, QUrl, QJsonObject, QString, DeepSeekAccount::JsonCallback done) {
            done({{"code", 0},
                  {"data", QJsonObject{{"biz_code", 0},
                                       {"biz_data",
                                        QJsonObject{{"authorize_id", "fixture"},
                                                    {"authorize_url", "https://evil.example/dsh/authorize"},
                                                    {"expires_in", 120}}}}}},
                 {});
        };
        account.login();
        QCOMPARE(opens, 0);
        QVERIFY(!account.signedIn());
    }
};
QTEST_MAIN(DeepSeekTest)
#include "tst_deepseek.moc"
