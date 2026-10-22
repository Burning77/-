#include "TextClient.h"
#include "SseParser.h"
#include "app/CredentialStore.h"
#include "auth/ChatGptAccount.h"
#include <QPointer>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTimer>
#include <memory>
namespace {
QString safeDiagnostic(QString text, const QString &key) {
    if (!key.isEmpty())
        text.replace(key, QStringLiteral("[密钥已隐藏]"));
    text.replace(QRegularExpression("Bearer\\s+[^\\s,;\"']+", QRegularExpression::CaseInsensitiveOption),
                 QStringLiteral("Bearer [密钥已隐藏]"));
    text.replace(QRegularExpression("sk-[A-Za-z0-9_-]+"), QStringLiteral("[密钥已隐藏]"));
    text.replace(QRegularExpression("[\\x00-\\x1f\\x7f]"), " ");
    return text.simplified().left(600);
}
QString serviceError(const QString &provider, int status, const QJsonValue &value, const QString &key) {
    const auto object = value.toObject();
    QStringList details;
    for (const auto &field : {QString("code"), QString("type")}) {
        const auto v = object.value(field);
        auto detail = v.isString() ? v.toString() : v.isDouble() ? QString::number(v.toDouble()) : QString();
        if (!detail.isEmpty() && !details.contains(detail))
            details.append(detail);
    }
    auto message = value.isString() ? value.toString() : object.value("message").toString();
    if (!message.isEmpty())
        details.append(message);
    if (details.isEmpty())
        details.append(QStringLiteral("服务未提供 JSON 错误详情，请检查地址、权限和网络"));
    auto prefix = status > 0 ? QStringLiteral("%1 请求失败（HTTP %2）：").arg(provider).arg(status)
                             : QStringLiteral("%1 请求失败：").arg(provider);
    return prefix + safeDiagnostic(details.join(" | "), key);
}
} // namespace
TextClient::TextClient(QObject *p) : QObject(p), manager(this) {
    keyReader = CredentialStore::read;
    connect(&ChatGptAccount::shared(), &ChatGptAccount::invalidated, this, [this](QString client) {
        for (auto id : profileRequests.keys())
            if (profileRequests.value(id) == client)
                cancel(id);
    });
}
QJsonObject TextClient::payload(const TextRequest &r) {
    QJsonObject o{{"model", r.config.model}, {"stream", true}};
    if (r.config.provider == "DeepSeek") {
        o["messages"] = r.messages;
        o["thinking"] = QJsonObject{{"type", "disabled"}};
        if (r.jsonMode)
            o["response_format"] = QJsonObject{{"type", "json_object"}};
    } else {
        if (r.config.provider == "ChatGPT") {
            QJsonArray input;
            QStringList instructions;
            for (auto value : r.messages) {
                auto message = value.toObject();
                if (message["role"] == "system")
                    instructions.append(message["content"].toString());
                else
                    input.append(message);
            }
            if (r.jsonMode)
                input.prepend(QJsonObject{
                    {"role", "developer"},
                    {"content", "Return a JSON object following the schema and rules in the instructions."}});
            o["input"] = input;
            o["store"] = false;
            if (!instructions.isEmpty())
                o["instructions"] = instructions.join("\n\n");
        } else
            o["input"] = r.messages;
        if (r.jsonMode)
            o["text"] = QJsonObject{{"format", QJsonObject{{"type", "json_object"}}}};
    }
    return o;
}
QString TextClient::start(const TextRequest &r) {
    if (r.config.provider == "ChatGPT") {
        if (QUrl(r.config.baseUrl).adjusted(QUrl::StripTrailingSlash).toString() !=
            "https://api.openai.com/v1") {
            QTimer::singleShot(0, this, [this, r] {
                emit failed(r.id, QStringLiteral("ChatGPT 登录授权仅可发送到官方 API 地址"));
            });
            return r.id;
        }
        pendingAuth.insert(r.id);
        profileRequests[r.id] = r.config.accountId;
        QPointer<TextClient> guard(this);
        ChatGptAccount::shared().token(r.config.accountId, [this, guard, r](QString token, QString error) {
            if (!guard || !pendingAuth.remove(r.id))
                return;
            if (!error.isEmpty()) {
                profileRequests.remove(r.id);
                QTimer::singleShot(0, this, [this, r, error] { emit failed(r.id, error); });
                return;
            }
            begin(r, token);
        });
        return r.id;
    }
    return begin(r, keyReader(r.config.credentialId()));
}
QString TextClient::begin(const TextRequest &r, const QString &key) {
    auto reject = [this, r](QString message) {
        QTimer::singleShot(0, this, [this, r, message] {
            profileRequests.remove(r.id);
            emit failed(r.id, message);
        });
        return r.id;
    };
    QUrl url(r.config.baseUrl);
    bool local = url.host() == "127.0.0.1" || url.host() == "localhost" || url.host() == "::1";
    if (!url.isValid() || url.host().isEmpty() || (!local && url.scheme() != "https") ||
        !url.userInfo().isEmpty())
        return reject(QStringLiteral("API 地址须为有效的 HTTPS 地址"));
    if (r.config.model.trimmed().isEmpty())
        return reject(QStringLiteral("请在设置中填写模型名称"));
    if (key.isEmpty())
        return reject(QStringLiteral("请在设置中填写该服务对应的 API Key"));
    auto path = url.path();
    while (path.endsWith('/'))
        path.chop(1);
    url.setPath(path + (r.config.provider == "DeepSeek" ? "/chat/completions" : "/responses"));
    url.setQuery(QString());
    url.setFragment(QString());
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Accept", "text/event-stream");
    req.setRawHeader("Authorization", "Bearer " + key.toUtf8());
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    req.setTransferTimeout(60000);
    auto reply = manager.post(req, QJsonDocument(payload(r)).toJson(QJsonDocument::Compact));
    replies.insert(r.id, reply);
    struct State {
        SseParser parser;
        bool terminal = false, ok = false;
        QString error;
        QByteArray errorBody;
        qint64 bytes = 0;
    };
    auto state = std::make_shared<State>();
    auto consume = [this, reply, r, state, key] {
        auto bytes = reply->readAll();
        state->bytes += bytes.size();
        if (state->bytes > 8 * 1024 * 1024) {
            state->error = QStringLiteral("响应过大，已停止生成");
            reply->abort();
            return;
        }
        if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) {
            state->errorBody += bytes.left(qMax(0, 65536 - int(state->errorBody.size())));
            return;
        }
        for (const auto &e : state->parser.feed(bytes)) {
            if (state->terminal)
                continue;
            if (e.data == "[DONE]" && r.config.provider == "DeepSeek") {
                state->terminal = true;
                state->ok = state->error.isEmpty();
                continue;
            }
            QJsonParseError parse;
            auto doc = QJsonDocument::fromJson(e.data.toUtf8(), &parse);
            if (parse.error != QJsonParseError::NoError) {
                state->error = QStringLiteral("服务返回无法解析的流事件");
                continue;
            }
            auto o = doc.object();
            auto type = o["type"].toString(e.event);
            if (type == "error" || o.value("error").isObject() || o.value("error").isString()) {
                const auto detail = o.value("error");
                state->error =
                    serviceError(r.config.provider, 200,
                                 detail.isObject() || detail.isString() ? detail : QJsonValue(o), key);
                state->terminal = true;
                continue;
            }
            if (r.config.provider == "DeepSeek") {
                auto choices = o["choices"].toArray();
                if (choices.isEmpty())
                    continue;
                auto choice = choices.first().toObject();
                auto text = choice["delta"].toObject()["content"].toString();
                if (!text.isEmpty())
                    emit textDelta(r.id, text);
                auto reason = choice["finish_reason"].toString();
                if (!reason.isEmpty() && reason != "stop")
                    state->error = QStringLiteral("模型输出未完整结束：") + reason;
            } else {
                if (type == "response.output_text.delta")
                    emit textDelta(r.id, o["delta"].toString());
                if (type == "response.completed") {
                    state->terminal = true;
                    state->ok = state->error.isEmpty();
                }
                if (type == "response.failed" || type == "response.incomplete") {
                    state->terminal = true;
                    auto response = o.value("response").toObject();
                    auto detail = response.value("error");
                    if (detail.isNull() || detail.isUndefined())
                        detail = QJsonObject{
                            {"code", response.value("incomplete_details").toObject().value("reason")},
                            {"message", QStringLiteral("模型输出未完成，请检查返回原因后重试")}};
                    state->error = serviceError(r.config.provider, 200, detail, key);
                }
            }
        }
    };
    connect(reply, &QNetworkReply::readyRead, this, consume);
    connect(reply, &QNetworkReply::finished, this, [this, reply, r, state, consume, key] {
        consume();
        replies.remove(r.id);
        profileRequests.remove(r.id);
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 200) {
            auto body = QJsonDocument::fromJson(state->errorBody).object();
            QJsonValue detail = body.value("error");
            if (detail.isUndefined() || detail.isNull())
                detail = body;
            if (!status && detail.toObject().isEmpty())
                detail = QJsonObject{{"code", "network_error"}, {"message", reply->errorString()}};
            emit failed(r.id, serviceError(r.config.provider, status, detail, key));
        } else if (!state->error.isEmpty())
            emit failed(r.id, state->error);
        else if (reply->error() != QNetworkReply::NoError)
            emit failed(r.id, QStringLiteral("网络中断、超时或请求已取消"));
        else if (!state->terminal || !state->ok)
            emit failed(r.id,
                        state->error.isEmpty() ? QStringLiteral("响应中断，已保留接收内容") : state->error);
        else
            emit finished(r.id);
        reply->deleteLater();
    });
    return r.id;
}
void TextClient::cancel(const QString &id) {
    if (pendingAuth.remove(id)) {
        profileRequests.remove(id);
        emit failed(id, QStringLiteral("用户已取消等待授权"));
        return;
    }
    if (replies.contains(id))
        replies[id]->abort();
}
