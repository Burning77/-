#include "DeepSeekAccount.h"
#include "OAuthProtocol.h"
#include "SecretStore.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QFile>
#include <QJsonDocument>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QSettings>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>
#include <QUuid>
namespace {
const QString platform = "https://platform.deepseek.com";
const QString api = "https://api.deepseek.com";
bool official(QUrl url, QString origin, QString path = {}) {
    return url.isValid() && url.scheme() == "https" && url.host() == QUrl(origin).host() &&
           url.port(443) == 443 && url.userInfo().isEmpty() && !url.hasFragment() &&
           (path.isEmpty() || url.path() == path);
}
QJsonObject unpack(QJsonObject root, QString &error) {
    auto data = root["data"].toObject();
    if (!root["code"].isDouble() || root["code"].toInt(-1) != 0 || !data["biz_code"].isDouble() ||
        data["biz_code"].toInt(-1) != 0 || !data["biz_data"].isObject()) {
        error = QStringLiteral("DeepSeek 授权未成功，请检查账号权限后重试（code %1 / %2）")
                    .arg(root["code"].toInt(-1))
                    .arg(data["biz_code"].toInt(-1));
        return {};
    }
    return data["biz_data"].toObject();
}
bool validToken(QString token) {
    if (token.isEmpty() || token.size() > 32768)
        return false;
    for (auto c : token)
        if (c.unicode() < 33 || c.unicode() > 126)
            return false;
    return true;
}
} // namespace
struct DeepSeekAccount::State {
    DeepSeekAccount *owner;
    QString file;
    QNetworkAccessManager network;
    QTcpServer listener;
    QTimer deadline;
    OAuthProtocol::Attempt attempt;
    QString authorizeId;
    quint64 serial = 0;
    bool exchanging = false;
    QList<QPair<QByteArray, QByteArray>> metadata;
    QJsonObject grant() const {
        auto value = SecretStore::read(file);
        return value["issuer"] == platform && validToken(value["token"].toString()) &&
                       !value["id"].toString().isEmpty()
                   ? value
                   : QJsonObject{};
    }
    void request(QString method, QUrl url, QJsonObject body, QString token, JsonCallback done,
                 QList<QPair<QByteArray, QByteArray>> meta = {}) {
        if (!official(url, platform) && !official(url, api)) {
            done({}, QStringLiteral("DeepSeek 授权仅可访问官方地址"));
            return;
        }
        if (owner->transport) {
            owner->transport(method, url, body, token, std::move(done));
            return;
        }
        QNetworkRequest request(url);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        request.setTransferTimeout(30000);
        request.setRawHeader("Accept", "application/json");
        request.setRawHeader("User-Agent", "QtInterviewAssistant/" INTERVIEW_APP_VERSION);
        if (official(url, platform)) {
            if (meta.isEmpty())
                meta = DeepSeekAccount::headers();
            for (auto h : meta)
                request.setRawHeader(h.first, h.second);
        }
        if (!token.isEmpty())
            request.setRawHeader("x-dsh-auth-token", token.toUtf8());
        QByteArray bytes;
        if (!body.isEmpty()) {
            request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
            bytes = QJsonDocument(body).toJson(QJsonDocument::Compact);
        }
        auto reply = method == "GET" ? network.get(request) : network.post(request, bytes);
        QTimer::singleShot(30000, reply, [reply] {
            if (!reply->isFinished())
                reply->abort();
        });
        auto result = std::make_shared<QByteArray>();
        auto tooLarge = std::make_shared<bool>(false);
        auto read = [reply, result, tooLarge] {
            auto bytes = reply->readAll();
            if (result->size() + bytes.size() > 65536) {
                *tooLarge = true;
                reply->abort();
                return;
            }
            *result += bytes;
        };
        QObject::connect(reply, &QNetworkReply::readyRead, owner, read);
        QObject::connect(
            reply, &QNetworkReply::finished, owner, [reply, result, tooLarge, read, done = std::move(done)] {
                read();
                auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                QString error;
                QJsonParseError parse;
                auto document = QJsonDocument::fromJson(*result, &parse);
                if (*tooLarge)
                    error = QStringLiteral("DeepSeek 响应过大，已停止读取");
                else if (status != 200 || reply->error() != QNetworkReply::NoError)
                    error = QStringLiteral("DeepSeek 请求失败（HTTP %1），请检查网络和账号权限").arg(status);
                else if (parse.error != QJsonParseError::NoError || !document.isObject())
                    error = QStringLiteral("DeepSeek 返回无效 JSON");
                reply->deleteLater();
                done(document.object(), error);
            });
    }
    void cancelRemote(QString id, QString verifier) {
        if (!id.isEmpty())
            request(
                "POST", QUrl(platform + "/auth-api/v0/dsh/auth_cancel"),
                {{"authorize_id", id}, {"code_verifier", verifier}}, {}, [](QJsonObject, QString) {},
                metadata);
    }
    void revoke(QString token, int retry = 0, QList<QPair<QByteArray, QByteArray>> meta = {}) {
        if (meta.isEmpty())
            meta = DeepSeekAccount::headers();
        QPointer<DeepSeekAccount> guard(owner);
        request(
            "POST", QUrl(platform + "/auth-api/v0/users/logout"), {}, token,
            [this, guard, token, retry, meta](QJsonObject result, QString error) {
                if (!guard)
                    return;
                if (error.isEmpty())
                    unpack(result, error);
                if (!error.isEmpty() && retry < 5)
                    QTimer::singleShot(1000 * (1 << retry), owner, [this, guard, token, retry, meta] {
                        if (guard)
                            revoke(token, retry + 1, meta);
                    });
            },
            meta);
    }
};
DeepSeekAccount::DeepSeekAccount(QObject *p, QString vault) : QObject(p), state(std::make_unique<State>()) {
    state->owner = this;
    state->file = (vault.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
                                         "/deepseek-account"
                                   : vault) +
                  "/session.dpapi";
    openBrowser = [](QUrl url) { return QDesktopServices::openUrl(url); };
    state->deadline.setSingleShot(true);
    connect(&state->deadline, &QTimer::timeout, this, [this] {
        cancelLogin();
        emit status(QStringLiteral("DeepSeek 登录已超时，请重新登录"));
    });
    connect(&state->listener, &QTcpServer::newConnection, this, [this] {
        while (state->listener.hasPendingConnections()) {
            auto socket = state->listener.nextPendingConnection();
            auto serial = state->serial;
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            QTimer::singleShot(10000, socket, [socket] { socket->disconnectFromHost(); });
            connect(socket, &QTcpSocket::readyRead, this, [this, socket, serial] {
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
                socket->setProperty("handled", true);
                auto line = bytes.left(bytes.indexOf("\r\n")).split(' ');
                auto attempt = state->attempt;
                auto target = line.size() == 3 ? QUrl::fromEncoded(line[1], QUrl::StrictMode) : QUrl();
                auto callback = OAuthProtocol::callback(QUrl(attempt.redirectUri).resolved(target), attempt);
                bool ok = serial == state->serial && !state->exchanging && line.size() == 3 &&
                          line[0] == "GET" && line[1].startsWith("/oauth/callback?") && target.isRelative() &&
                          target.host().isEmpty() && callback.error.isEmpty();
                QByteArray page = ok ? "Authorization received. Return to Interview Assistant."
                                     : "Invalid callback. Return to the app and retry.";
                socket->write("HTTP/1.1 " + QByteArray(ok ? "200 OK" : "400 Bad Request") +
                              "\r\nContent-Type: text/plain; charset=utf-8\r\nCache-Control: "
                              "no-store\r\nConnection: close\r\nContent-Length: " +
                              QByteArray::number(page.size()) + "\r\n\r\n" + page);
                socket->disconnectFromHost();
                if (!ok) {
                    emit status(QStringLiteral("DeepSeek 登录回调校验未通过"));
                    return;
                }
                state->exchanging = true;
                state->listener.close();
                QSettings settings;
                auto device = settings.value("DeepSeek/deviceId").toString();
                if (device.isEmpty()) {
                    device = QUuid::createUuid().toString(QUuid::WithoutBraces);
                    settings.setValue("DeepSeek/deviceId", device);
                }
                QPointer<DeepSeekAccount> guard(this);
                state->request(
                    "POST", QUrl(platform + "/auth-api/v0/dsh/auth_exchange"),
                    {{"code", callback.code},
                     {"code_verifier", attempt.verifier},
                     {"redirect_uri", attempt.redirectUri},
                     {"device_id", device},
                     {"device_model", QString("win32-") + QSysInfo::currentCpuArchitecture()},
                     {"os_version", QString("win32 ") + QSysInfo::kernelVersion()}},
                    {},
                    [this, guard, serial](QJsonObject response, QString error) {
                        if (!guard || serial != state->serial)
                            return;
                        auto data = error.isEmpty() ? unpack(response, error) : QJsonObject{};
                        auto token = data["token"].toString();
                        QUrl complete(data["authorized_url"].toString());
                        if (error.isEmpty() &&
                            (!validToken(token) || !official(complete, platform, "/dsh/authorized")))
                            error = QStringLiteral("DeepSeek 授权响应缺少有效令牌或官方完成地址");
                        QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                        auto old = sessionId();
                        if (error.isEmpty() &&
                            !SecretStore::write(state->file,
                                                {{"issuer", platform}, {"token", token}, {"id", id}}))
                            error = QStringLiteral("DeepSeek 登录凭据加密保存失败");
                        if (!error.isEmpty()) {
                            cancelLogin();
                            emit status(error);
                            return;
                        }
                        ++state->serial;
                        state->deadline.stop();
                        state->attempt = {};
                        state->authorizeId.clear();
                        state->exchanging = false;
                        QSettings().setValue("DeepSeek/sessionId", id);
                        QSettings().setValue("DeepSeek/accountLogin", true);
                        if (!old.isEmpty())
                            emit invalidated(old);
                        emit changed();
                        emit status(QStringLiteral("DeepSeek 账号已登录；可刷新模型并测试连接"));
                        QUrlQuery query(complete);
                        query.removeAllQueryItems("login_source");
                        query.addQueryItem("login_source", "desktop");
                        complete.setQuery(query);
                        openBrowser(complete);
                    },
                    state->metadata);
            });
        }
    });
}
DeepSeekAccount::~DeepSeekAccount() {
    // Disconnect callbacks before the pimpl and its network/socket members are destroyed.
    state->deadline.stop();
    state->listener.close();
    disconnect(&state->deadline, nullptr, this, nullptr);
    disconnect(&state->listener, nullptr, this, nullptr);
    for (auto socket : state->listener.findChildren<QTcpSocket *>())
        disconnect(socket, nullptr, this, nullptr);
    for (auto reply : state->network.findChildren<QNetworkReply *>()) {
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
    }
}
DeepSeekAccount &DeepSeekAccount::shared() {
    static QPointer<DeepSeekAccount> value;
    if (!value)
        value = new DeepSeekAccount(QCoreApplication::instance());
    return *value;
}
bool DeepSeekAccount::signedIn() const {
    return !state->grant().isEmpty();
}
QString DeepSeekAccount::sessionId() const {
    return state->grant()["id"].toString();
}
void DeepSeekAccount::login() {
    cancelLogin();
    if (!state->listener.listen(QHostAddress::LocalHost, 0)) {
        emit status(QStringLiteral("无法启动 DeepSeek 本机登录端口"));
        return;
    }
    state->attempt = OAuthProtocol::create("deepseek", state->listener.serverPort());
    state->attempt.redirectUri =
        QString("http://127.0.0.1:%1/oauth/callback").arg(state->listener.serverPort());
    state->metadata = headers();
    state->deadline.start(600000);
    auto serial = state->serial;
    auto attempt = state->attempt;
    QPointer<DeepSeekAccount> guard(this);
    emit status(QStringLiteral("正在申请 DeepSeek 浏览器授权…"));
    state->request(
        "POST", QUrl(platform + "/auth-api/v0/dsh/auth_init"),
        {{"code_challenge", OAuthProtocol::challenge(attempt.verifier)},
         {"code_challenge_method", "S256"},
         {"state", attempt.state},
         {"redirect_uri", attempt.redirectUri},
         {"locale", QLocale::system().language() == QLocale::Chinese ? "zh_CN" : "en_US"},
         {"login_source", "desktop"}},
        {},
        [this, guard, serial, attempt](QJsonObject response, QString error) {
            if (!guard)
                return;
            auto data = error.isEmpty() ? unpack(response, error) : QJsonObject{};
            auto authId = data["authorize_id"].toString();
            if (serial != state->serial) {
                state->cancelRemote(authId, attempt.verifier);
                return;
            }
            QUrl browser(data["authorize_url"].toString());
            if (error.isEmpty() && (authId.isEmpty() || !official(browser, platform, "/dsh/authorize") ||
                                    data["expires_in"].toDouble() <= 0))
                error = QStringLiteral("DeepSeek 返回无效的授权地址或有效期");
            state->authorizeId = authId;
            if (!error.isEmpty()) {
                cancelLogin();
                emit status(error);
                return;
            }
            int remaining = state->deadline.remainingTime();
            state->deadline.start(qMin(remaining, int(qMin(600.0, data["expires_in"].toDouble()) * 1000)));
            if (!openBrowser(browser)) {
                cancelLogin();
                emit status(QStringLiteral("无法打开默认浏览器"));
                return;
            }
            emit status(QStringLiteral("请在浏览器中登录并授权 DeepSeek 平台账号"));
        },
        state->metadata);
}
void DeepSeekAccount::cancelLogin() {
    auto id = state->authorizeId, verifier = state->attempt.verifier;
    ++state->serial;
    state->listener.close();
    state->deadline.stop();
    state->attempt = {};
    state->authorizeId.clear();
    state->exchanging = false;
    state->cancelRemote(id, verifier);
}
void DeepSeekAccount::signOut() {
    cancelLogin();
    auto grant = state->grant();
    auto id = grant["id"].toString();
    if (QFile::exists(state->file) && !QFile::remove(state->file)) {
        emit status(QStringLiteral("无法删除本地 DeepSeek 凭据，请检查文件权限"));
        return;
    }
    if (QSettings().value("DeepSeek/sessionId").toString() == id)
        QSettings().remove("DeepSeek/sessionId");
    if (!id.isEmpty())
        emit invalidated(id);
    emit changed();
    emit status(QStringLiteral("已退出 DeepSeek；账号模式不会自动改用 API Key"));
    auto token = grant["token"].toString();
    if (!token.isEmpty())
        state->revoke(token);
}
void DeepSeekAccount::token(QString session, TokenCallback done) {
    auto grant = state->grant();
    if (session.isEmpty() || session != grant["id"].toString()) {
        done({}, QStringLiteral("请先登录 DeepSeek 账号，或重新提交请求"));
        return;
    }
    done(grant["token"].toString(), {});
}
void DeepSeekAccount::models(JsonCallback done) {
    auto id = sessionId();
    token(id, [this, done](QString token, QString error) {
        if (!error.isEmpty()) {
            done({}, error);
            return;
        }
        // /models is an independent catalogue; a 401 here must not delete the grant.
        state->request("GET", QUrl(api + "/models"), {}, token, done);
    });
}
void DeepSeekAccount::rejectToken(QString session) {
    if (session.isEmpty() || session != sessionId())
        return;
    if (!QFile::remove(state->file)) {
        emit status(QStringLiteral("DeepSeek 授权失效，凭据文件删除失败"));
        return;
    }
    QSettings().remove("DeepSeek/sessionId");
    emit invalidated(session);
    emit changed();
    emit status(QStringLiteral("DeepSeek 授权已失效，请重新登录"));
}
QList<QPair<QByteArray, QByteArray>> DeepSeekAccount::headers() {
    return {{"x-client-bundle-id", {}},
            {"x-client-platform", "desktop-win"},
            {"x-client-version", INTERVIEW_APP_VERSION},
            {"x-client-locale", QLocale::system().language() == QLocale::Chinese ? "zh_CN" : "en_US"},
            {"x-client-timezone-offset", QByteArray::number(QDateTime::currentDateTime().offsetFromUtc())}};
}
