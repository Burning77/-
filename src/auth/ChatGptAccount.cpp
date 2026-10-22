#include "ChatGptAccount.h"
#include "SecretStore.h"
#include "domain/Types.h"
#include <QCoreApplication>
#include <QDesktopServices>
#include <QFile>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QPointer>
#include <QSettings>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QUrlQuery>
ChatGptAccount &ChatGptAccount::shared() {
    static QPointer<ChatGptAccount> value;
    if (!value)
        value = new ChatGptAccount(QCoreApplication::instance());
    return *value;
}
ChatGptAccount::ChatGptAccount(QObject *p, QString root) : QObject(p), network(this), listener(this) {
    vault = root.isEmpty()
                ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/chatgpt-secrets"
                : root;
    now = []() { return QDateTime::currentSecsSinceEpoch(); };
    openBrowser = [](QUrl url) { return QDesktopServices::openUrl(url); };
    transport = [this](QString method, QUrl url, QByteArray body, QString bearer, JsonCallback cb) {
        request(method, url, body, bearer, std::move(cb));
    };
    loginTimeout.setSingleShot(true);
    loginTimeout.setInterval(180000);
    connect(&loginTimeout, &QTimer::timeout, this, [this] {
        cancelLogin();
        emit status(QStringLiteral("登录等待超时，请重新点击登录"));
    });
    connect(&listener, &QTcpServer::newConnection, this, &ChatGptAccount::acceptCallback);
}
QJsonArray ChatGptAccount::profiles() const {
    return QJsonDocument::fromJson(QSettings().value("chatgpt/profiles", "[]").toByteArray()).array();
}
QString ChatGptAccount::activeProfile() const {
    return QSettings().value("chatgpt/active").toString();
}
QJsonObject ChatGptAccount::profile(QString client) const {
    for (auto v : profiles())
        if (v.toObject()["clientId"] == client)
            return v.toObject();
    return {};
}
void ChatGptAccount::updateProfile(QJsonObject value) {
    QJsonArray out;
    bool replaced = false;
    for (auto v : profiles())
        if (v.toObject()["clientId"] == value["clientId"]) {
            out.append(value);
            replaced = true;
        } else
            out.append(v);
    if (!replaced)
        out.append(value);
    QSettings().setValue("chatgpt/profiles", QJsonDocument(out).toJson(QJsonDocument::Compact));
}
QString ChatGptAccount::path(QString client) const {
    return vault + "/" +
           QString::fromLatin1(
               QCryptographicHash::hash(client.toUtf8(), QCryptographicHash::Sha256).toHex()) +
           ".dpapi";
}
void ChatGptAccount::invalidate(QString client, bool discardRefresh) {
    epochs[client]++;
    if (discardRefresh) {
        credentialEpochs[client]++;
        refreshInFlight.remove(client);
    }
    auto callbacks = refreshing.take(client);
    for (auto cb : callbacks)
        cb({}, QStringLiteral("账户状态已改变，请重新提交"));
    emit invalidated(client);
}
void ChatGptAccount::selectProfile(QString client) {
    if (!client.isEmpty() && profile(client).isEmpty())
        return;
    auto old = activeProfile();
    if (old == client)
        return;
    cancelLogin();
    QSettings().setValue("chatgpt/active", client);
    if (!old.isEmpty())
        invalidate(old);
    emit changed();
}
void ChatGptAccount::cancelLogin() {
    loginSerial++;
    listener.close();
    loginTimeout.stop();
    attempt = {};
}
void ChatGptAccount::login(QString client) {
    cancelLogin();
    if (!client.isEmpty() && profile(client).isEmpty()) {
        emit status(QStringLiteral("未知账户注册"));
        return;
    }
    if (!listener.listen(QHostAddress::LocalHost, 0)) {
        emit status(QStringLiteral("无法启动本机登录回调端口"));
        return;
    }
    QSettings settings;
    auto host = settings.value("chatgpt/hostId").toString();
    if (host.isEmpty()) {
        host = "urn:uuid:" + newId();
        settings.setValue("chatgpt/hostId", host);
    }
    attempt =
        OAuthProtocol::create(client.isEmpty() ? "dynamic_agent_client" : client, listener.serverPort());
    auto url = OAuthProtocol::authorization(attempt, host);
    if (!client.isEmpty() && !profile(client)["planEnabled"].toBool()) {
        QUrlQuery q(url);
        q.addQueryItem("prompt", "consent");
        url.setQuery(q);
    }
    loginTimeout.start();
    if (!openBrowser(url)) {
        cancelLogin();
        emit status(QStringLiteral("无法打开浏览器，请检查默认浏览器设置"));
        return;
    }
    emit status(QStringLiteral("请在浏览器中登录，并允许本工具使用 ChatGPT 套餐"));
}
void ChatGptAccount::acceptCallback() {
    while (listener.hasPendingConnections()) {
        auto socket = listener.nextPendingConnection();
        auto serial = loginSerial;
        auto read = [this, socket, serial] {
            if (socket->property("handled").toBool())
                return;
            auto bytes = socket->property("request").toByteArray() + socket->readAll();
            socket->setProperty("request", bytes);
            if (bytes.size() > 16384) {
                socket->abort();
                return;
            }
            if (!bytes.contains("\r\n\r\n"))
                return;
            auto line = bytes.left(bytes.indexOf("\r\n")).split(' ');
            if (line.size() != 3 || line[0] != "GET" || !line[1].startsWith("/auth/callback?") ||
                serial != loginSerial) {
                socket->disconnectFromHost();
                return;
            }
            auto url = QUrl(attempt.redirectUri).resolved(QUrl::fromEncoded(line[1]));
            auto result = OAuthProtocol::callback(url, attempt);
            socket->setProperty("handled", true);
            const bool ok = result.error.isEmpty();
            QByteArray page = ok ? "Login received. You may return to Interview Assistant."
                                 : "Login was not accepted. Return to the application and retry.";
            socket->write("HTTP/1.1 " + QByteArray(ok ? "200 OK" : "400 Bad Request") +
                          "\r\nContent-Type: text/plain; charset=utf-8\r\nCache-Control: "
                          "no-store\r\nConnection: close\r\nContent-Length: " +
                          QByteArray::number(page.size()) + "\r\n\r\n" + page);
            socket->disconnectFromHost();
            if (!ok) {
                emit status(result.error);
                return;
            }
            listener.close();
            loginTimeout.stop();
            const auto exchangeSerial = ++loginSerial;
            auto p = profile(result.clientId);
            if (p.isEmpty()) {
                p = {{"clientId", result.clientId}, {"label", QStringLiteral("待完成注册")}};
                updateProfile(p);
            }
            exchange(result.code, result.clientId, exchangeSerial);
        };
        connect(socket, &QTcpSocket::readyRead, socket, read);
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        QTimer::singleShot(10000, socket, [socket] { socket->disconnectFromHost(); });
        if (socket->bytesAvailable())
            read();
    }
}
void ChatGptAccount::request(QString method, QUrl url, QByteArray body, QString bearer, JsonCallback done) {
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    req.setTransferTimeout(20000);
    if (!bearer.isEmpty())
        req.setRawHeader("Authorization", "Bearer " + bearer.toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    auto reply = method == "GET" ? network.get(req) : network.post(req, body);
    auto bytes = std::make_shared<QByteArray>();
    connect(reply, &QNetworkReply::readyRead, this, [reply, bytes] {
        *bytes += reply->readAll();
        if (bytes->size() > 1024 * 1024)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [reply, bytes, done] {
        if (reply->isOpen())
            *bytes += reply->readAll();
        int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        auto doc = QJsonDocument::fromJson(*bytes);
        bool ok =
            http == 200 && reply->error() == QNetworkReply::NoError && (doc.isObject() || bytes->isEmpty());
        done(doc.object(),
             ok ? QString()
                : QStringLiteral("授权服务请求失败（HTTP %1），请检查网络、授权或重新登录").arg(http));
        reply->deleteLater();
    });
}
void ChatGptAccount::exchange(QString code, QString client, quint64 serial) {
    QUrlQuery form;
    for (auto p : QList<QPair<QString, QString>>{{"grant_type", "authorization_code"},
                                                 {"client_id", client},
                                                 {"code", code},
                                                 {"code_verifier", attempt.verifier},
                                                 {"redirect_uri", attempt.redirectUri},
                                                 {"resource", "https://api.openai.com/v1"}})
        form.addQueryItem(p.first, p.second);
    const auto nonce = attempt.nonce;
    transport("POST", QUrl("https://auth.openai.com/api/accounts/oauth/token"),
              form.query(QUrl::FullyEncoded).toUtf8(), {},
              [this, serial, client, nonce](QJsonObject tokens, QString error) {
                  if (serial != loginSerial)
                      return;
                  if (!error.isEmpty()) {
                      emit status(error);
                      return;
                  }
                  transport(
                      "GET", QUrl("https://auth.openai.com/.well-known/jwks.json"), {}, {},
                      [this, serial, client, nonce, tokens](QJsonObject jwks, QString error) {
                          if (serial != loginSerial)
                              return;
                          QString validation;
                          auto claims = OAuthProtocol::verifyIdToken(tokens["id_token"].toString(), jwks,
                                                                     client, nonce, now(), &validation);
                          if (!error.isEmpty() || !validation.isEmpty()) {
                              emit status(QStringLiteral("身份验证失败，请重新登录"));
                              return;
                          }
                          saveTokens(client, tokens, claims, false,
                                     [this, serial, client](QJsonObject, QString error) {
                                         if (serial != loginSerial)
                                             return;
                                         if (!error.isEmpty()) {
                                             emit status(error);
                                             return;
                                         }
                                         auto old = activeProfile();
                                         QSettings().setValue("chatgpt/active", client);
                                         if (old != client && !old.isEmpty())
                                             invalidate(old);
                                         attempt = {};
                                         emit changed();
                                         emit status(
                                             profile(client)["planEnabled"].toBool()
                                                 ? QStringLiteral("ChatGPT 登录成功，套餐使用已授权")
                                                 : QStringLiteral("已登录，但尚未授权套餐使用；请再次授权"));
                                     });
                      });
              });
}
void ChatGptAccount::saveTokens(QString client, QJsonObject tokens, QJsonObject claims, bool refreshingFlag,
                                JsonCallback done) {
    auto old = profile(client);
    if (!old["sub"].toString().isEmpty() && old["sub"] != claims["sub"]) {
        done({}, QStringLiteral("返回的身份与原账户不匹配"));
        return;
    }
    auto previous = SecretStore::read(path(client));
    if (refreshingFlag && !tokens.contains("scope"))
        tokens["scope"] = previous["scope"];
    if (tokens["access_token"].toString().isEmpty() || tokens["refresh_token"].toString().isEmpty() ||
        tokens["expires_in"].toInteger() <= 0 ||
        tokens["token_type"].toString().compare("Bearer", Qt::CaseInsensitive) != 0) {
        done({}, QStringLiteral("授权服务未返回完整凭据"));
        return;
    }
    tokens["expiresAt"] = now() + tokens["expires_in"].toInteger();
    tokens["clientId"] = client;
    if (!SecretStore::write(path(client), tokens)) {
        done({}, QStringLiteral("Windows 本地凭据保护保存失败"));
        return;
    }
    if (!refreshingFlag)
        invalidate(client, true);
    old["clientId"] = client;
    old["sub"] = claims["sub"];
    old["label"] = claims["email"].toString(claims["sub"].toString().left(12));
    old["planEnabled"] = tokens["scope"].toString().split(' ').contains("chatgpt.tokens.use.direct");
    updateProfile(old);
    done(old, {});
}
void ChatGptAccount::token(QString client, TokenCallback done) {
    if (client != activeProfile()) {
        done({}, QStringLiteral("当前 ChatGPT 账户已改变，请重新提交请求"));
        return;
    }
    if (client.isEmpty() || profile(client).isEmpty()) {
        done({}, QStringLiteral("请先使用 ChatGPT 登录"));
        return;
    }
    auto credentials = SecretStore::read(path(client));
    if (!credentials["scope"].toString().split(' ').contains("chatgpt.tokens.use.direct")) {
        done({}, QStringLiteral("请登录并允许使用 ChatGPT 套餐"));
        return;
    }
    if (credentials["expiresAt"].toInteger() > now() + 60 &&
        !credentials["access_token"].toString().isEmpty()) {
        done(credentials["access_token"].toString(), {});
        return;
    }
    if (credentials["refresh_token"].toString().isEmpty()) {
        done({}, QStringLiteral("登录已过期，请重新登录"));
        return;
    }
    refreshing[client].append(done);
    if (refreshInFlight.contains(client))
        return;
    refreshInFlight.insert(client);
    auto epoch = credentialEpochs.value(client);
    QUrlQuery form;
    form.addQueryItem("grant_type", "refresh_token");
    form.addQueryItem("client_id", client);
    form.addQueryItem("refresh_token", credentials["refresh_token"].toString());
    form.addQueryItem("resource", "https://api.openai.com/v1");
    auto finish = [this, client, epoch](QString token, QString error) {
        if (epoch != credentialEpochs.value(client))
            return;
        refreshInFlight.remove(client);
        auto callbacks = refreshing.take(client);
        for (auto cb : callbacks)
            cb(token, error);
    };
    transport("POST", QUrl("https://auth.openai.com/api/accounts/oauth/token"),
              form.query(QUrl::FullyEncoded).toUtf8(), {},
              [this, client, epoch, finish](QJsonObject tokens, QString error) {
                  if (epoch != credentialEpochs.value(client))
                      return;
                  if (!error.isEmpty()) {
                      finish({}, error);
                      return;
                  }
                  transport("GET", QUrl("https://auth.openai.com/.well-known/jwks.json"), {}, {},
                            [this, client, epoch, tokens, finish](QJsonObject jwks, QString error) {
                                if (epoch != credentialEpochs.value(client))
                                    return;
                                QString validation;
                                auto claims = OAuthProtocol::verifyIdToken(
                                    tokens["id_token"].toString(), jwks, client, {}, now(), &validation);
                                if (!error.isEmpty() || !validation.isEmpty()) {
                                    finish({}, QStringLiteral("登录续期身份校验失败，请重新登录"));
                                    return;
                                }
                                saveTokens(client, tokens, claims, true,
                                           [this, tokens, finish](QJsonObject, QString error) {
                                               finish(error.isEmpty() ? tokens["access_token"].toString()
                                                                      : QString(),
                                                      error);
                                           });
                            });
              });
}
void ChatGptAccount::models(QString client, JsonCallback done) {
    QPointer<ChatGptAccount> guard(this);
    auto epoch = epochs.value(client);
    token(client, [this, guard, client, epoch, done](QString token, QString error) {
        if (!guard)
            return;
        if (!error.isEmpty()) {
            done({}, error);
            return;
        }
        transport("GET", QUrl("https://api.openai.com/v1/models"), {}, token,
                  [this, client, epoch, done](QJsonObject o, QString e) {
                      if (epoch != epochs.value(client)) {
                          done({}, QStringLiteral("账户已切换"));
                          return;
                      }
                      done(o, e);
                  });
    });
}
void ChatGptAccount::signOut(QString client) {
    if (client.isEmpty() || profile(client).isEmpty())
        return;
    cancelLogin();
    if (activeProfile() == client)
        QSettings().setValue("chatgpt/active", QString());
    invalidate(client, true);
    auto credentials = SecretStore::read(path(client));
    bool cleared = !QFile::exists(path(client)) || QFile::remove(path(client));
    if (!cleared) {
        emit status(QStringLiteral("本地登录凭据清理失败，请检查文件权限"));
        return;
    }
    auto p = profile(client);
    p["planEnabled"] = false;
    updateProfile(p);
    if (activeProfile() == client)
        QSettings().setValue("chatgpt/active", QString());
    emit changed();
    if (credentials["refresh_token"].toString().isEmpty()) {
        emit status(QStringLiteral("已退出登录"));
        return;
    }
    QUrlQuery form;
    form.addQueryItem("token", credentials["refresh_token"].toString());
    form.addQueryItem("token_type_hint", "refresh_token");
    form.addQueryItem("client_id", client);
    revoke(form.query(QUrl::FullyEncoded).toUtf8());
}
void ChatGptAccount::revoke(QByteArray body, int attempt) {
    transport("POST", QUrl("https://auth.openai.com/api/accounts/oauth/revoke"), body, {},
              [this, body, attempt](QJsonObject, QString error) {
                  if (!error.isEmpty() && attempt < 2) {
                      QTimer::singleShot(attempt == 0 ? 500 : 1500, this,
                                         [this, body, attempt] { revoke(body, attempt + 1); });
                      return;
                  }
                  emit status(
                      error.isEmpty()
                          ? QStringLiteral("已退出并撤销登录会话")
                          : QStringLiteral("已在本地退出；未确认远端撤销，可在 ChatGPT 设置中断开本工具"));
              });
}
