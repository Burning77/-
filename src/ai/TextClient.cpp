#include "TextClient.h"
#include "SseParser.h"
#include "app/CredentialStore.h"
#include "auth/ChatGptAccount.h"
#include "auth/DeepSeekAccount.h"
#include <QPointer>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTimer>
#include <QSslError>
#include <QSslConfiguration>
#include <QSslCipher>
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
TextClient::TextClient(QObject *p, DeepSeekAccount *deepSeek)
    : QObject(p), account(deepSeek ? deepSeek : &DeepSeekAccount::shared()), manager(this) {
    keyReader = CredentialStore::read;
    postRequest = [this](const QNetworkRequest &request, const QByteArray &body) {
        return manager.post(request, body);
    };
    connect(account, &DeepSeekAccount::invalidated, this, [this](QString session) {
        for (auto id : profileRequests.keys())
            if (profileRequests.value(id) == "deepseek:" + session)
                cancel(id);
    });
    connect(&ChatGptAccount::shared(), &ChatGptAccount::invalidated, this, [this](QString client) {
        for (auto id : profileRequests.keys())
            if (profileRequests.value(id) == client)
                cancel(id);
    });
}
QJsonObject TextClient::payload(const TextRequest &r) {
    QJsonObject o{{"model", r.config.model}, {"stream", true}};
    if (r.config.provider == "DeepSeek") {
        if (r.config.accountLogin) {
            QJsonArray messages;
            QStringList system;
            for (auto value : r.messages) {
                auto message = value.toObject();
                if (message["role"] == "system" || message["role"] == "developer")
                    system.append(message["content"].toString());
                else
                    messages.append(QJsonObject{
                        {"role", message["role"]},
                        {"content",
                         QJsonArray{QJsonObject{{"type", "text"}, {"text", message["content"]}}}}});
            }
            if (r.jsonMode)
                system.append("Return a JSON object only, following the requested schema.");
            o["messages"] = messages;
            o["max_tokens"] = 4096;
            if (!system.isEmpty())
                o["system"] = system.join("\n\n");
        } else
            o["messages"] = r.messages;
        o["thinking"] = QJsonObject{{"type", "disabled"}};
        if (r.jsonMode && !r.config.accountLogin)
            o["response_format"] = QJsonObject{{"type", "json_object"}};
    } else {
        if (r.config.provider == "ChatGPT") {
            if (!r.config.reasoningEffort.isEmpty())
                o["reasoning"] = QJsonObject{{"effort", r.config.reasoningEffort}};
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
        if (r.operation == "company_research") {
            o["tools"] = QJsonArray{QJsonObject{{"type", "web_search"}}};
            o["tool_choice"] = "required";
            o["include"] = QJsonArray{"web_search_call.action.sources"};
            o.remove("text");
        }
    }
    if (!r.imagePng.isEmpty()) {
        const auto name = r.config.provider == "DeepSeek" ? "messages" : "input";
        auto messages = o[name].toArray();
        for (qsizetype i = messages.size(); i > 0; --i) {
            auto message = messages[i - 1].toObject();
            if (message["role"] != "user")
                continue;
            QJsonArray parts = message["content"].toArray();
            if (!message["content"].isArray())
                parts.append(QJsonObject{{"type", r.config.provider == "DeepSeek" ? "text" : "input_text"},
                                         {"text", message["content"]}});
            const auto base64 = QString::fromLatin1(r.imagePng.toBase64());
            if (r.config.provider == "DeepSeek" && r.config.accountLogin)
                parts.append(QJsonObject{
                    {"type", "image"},
                    {"source",
                     QJsonObject{{"type", "base64"}, {"media_type", "image/png"}, {"data", base64}}}});
            else if (r.config.provider == "DeepSeek")
                parts.append(QJsonObject{{"type", "image_url"},
                                         {"image_url", QJsonObject{{"url", "data:image/png;base64," + base64},
                                                                   {"detail", "high"}}}});
            else
                parts.append(QJsonObject{{"type", "input_image"},
                                         {"image_url", "data:image/png;base64," + base64},
                                         {"detail", "high"}});
            message["content"] = parts;
            messages[i - 1] = message;
            break;
        }
        o[name] = messages;
    }
    return o;
}
QString TextClient::start(const TextRequest &r) {
    if (!r.imagePng.isEmpty()) {
        QString error;
        if (r.imagePng.size() > 10 * 1024 * 1024 ||
            !r.imagePng.startsWith(QByteArray::fromHex("89504e470d0a1a0a")))
            error = QStringLiteral("截图必须为有效的PNG，且不超过10MB，请重新截图");
        else if (r.config.provider == "DeepSeek" && r.config.model != "deepseek-flash" &&
                 r.config.model != "deepseek-v4-flash-vision-exp")
            error = QStringLiteral("当前DeepSeek模型不支持本软件的截图输入，请选择 deepseek-flash "
                                   "或支持图片的 ChatGPT / OpenAI 模型");
        if (!error.isEmpty()) {
            QTimer::singleShot(0, this, [this, id = r.id, error] { emit failed(id, error); });
            return r.id;
        }
    }
    if (r.operation == "company_research" && r.config.provider != "ChatGPT" &&
        r.config.provider != "OpenAI") {
        QTimer::singleShot(0, this, [this, r] {
            emit failed(r.id, QStringLiteral(
                                  "当前联网核验支持 ChatGPT / OpenAI；请切换服务，或取消联网仅分析填写资料"));
        });
        return r.id;
    }
    if (r.config.provider == "DeepSeek" && r.config.accountLogin) {
        auto url = QUrl(r.config.baseUrl);
        if (url.adjusted(QUrl::StripTrailingSlash).toString() != "https://api.deepseek.com" &&
            url.adjusted(QUrl::StripTrailingSlash).toString() != "https://api.deepseek.com/v1") {
            QTimer::singleShot(0, this, [this, r] {
                emit failed(r.id, QStringLiteral("DeepSeek 账号授权仅可发送到官方 API 地址"));
            });
            return r.id;
        }
        pendingAuth.insert(r.id);
        profileRequests[r.id] = "deepseek:" + r.config.accountId;
        QPointer<TextClient> guard(this);
        if (!account) {
            pendingAuth.remove(r.id);
            profileRequests.remove(r.id);
            QTimer::singleShot(0, this,
                               [this, r] { emit failed(r.id, QStringLiteral("DeepSeek 账号服务已关闭")); });
            return r.id;
        }
        account->token(r.config.accountId, [this, guard, r](QString token, QString error) {
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
QString TextClient::begin(const TextRequest &r, const QString &key, int retry) {
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
    bool deepSeekAccount = r.config.provider == "DeepSeek" && r.config.accountLogin;
    url.setPath(deepSeekAccount
                    ? "/anthropic/v1/messages"
                    : path + (r.config.provider == "DeepSeek" ? "/chat/completions" : "/responses"));
    url.setQuery(QString());
    url.setFragment(QString());
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Accept", "text/event-stream");
    if (deepSeekAccount) {
        req.setRawHeader("x-dsh-auth-token", key.toUtf8());
        req.setRawHeader("anthropic-version", "2023-06-01");
        req.setRawHeader("User-Agent", "QtInterviewAssistant/" INTERVIEW_APP_VERSION);
    } else
        req.setRawHeader("Authorization", "Bearer " + key.toUtf8());
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    req.setTransferTimeout(60000);
    auto reply = postRequest(req, QJsonDocument(payload(r)).toJson(QJsonDocument::Compact));
    replies.insert(r.id, reply);
    struct State {
        SseParser parser;
        bool terminal = false, ok = false;
        bool connecting = false, encrypted = false, sent = false, sslError = false;
        bool searched = false;
        QJsonArray sources;
        QString error, stopReason;
        QByteArray errorBody;
        qint64 bytes = 0;
    };
    auto state = std::make_shared<State>();
    auto collectSources = [state](const QJsonObject &item) {
        auto add = [state](const QJsonObject &source) {
            const auto value = source["url"].toString();
            QUrl url(value);
            if (value.size() > 2048 || !url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty() ||
                (url.scheme() != "https" && url.scheme() != "http"))
                return;
            const auto normalized = url.toString(QUrl::FullyEncoded);
            auto title = source["title"].toString().trimmed().left(300);
            for (qsizetype i = 0; i < state->sources.size(); ++i) {
                auto existing = state->sources[i].toObject();
                if (existing["url"] == normalized) {
                    if (!title.isEmpty() && existing["title"] == normalized) {
                        existing["title"] = title;
                        state->sources[i] = existing;
                    }
                    return;
                }
            }
            if (state->sources.size() < 12)
                state->sources.append(
                    QJsonObject{{"url", normalized}, {"title", title.isEmpty() ? normalized : title}});
        };
        if (item["type"] == "web_search_call" && item["status"] == "completed") {
            state->searched = true;
            for (auto source : item["action"].toObject()["sources"].toArray())
                add(source.toObject());
        }
        if (item["type"] == "message")
            for (auto content : item["content"].toArray())
                for (auto annotation : content.toObject()["annotations"].toArray())
                    if (annotation.toObject()["type"] == "url_citation")
                        add(annotation.toObject());
    };
    const auto origin = url.adjusted(QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment).toString();
    connect(reply, &QNetworkReply::socketStartedConnecting, this, [state] { state->connecting = true; });
    connect(reply, &QNetworkReply::encrypted, this, [this, state, origin] {
        state->encrypted = true;
        establishedTlsOrigins.insert(origin);
    });
    connect(reply, &QNetworkReply::requestSent, this, [state] { state->sent = true; });
    connect(reply, &QNetworkReply::sslErrors, this, [state] { state->sslError = true; });
    auto consume = [this, reply, r, state, key, collectSources] {
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
            if (e.data == "[DONE]" && r.config.provider == "DeepSeek" && !r.config.accountLogin) {
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
            if (r.operation == "company_research") {
                if (type == "response.output_item.done")
                    collectSources(o["item"].toObject());
                if (type == "response.completed")
                    for (auto item : o["response"].toObject()["output"].toArray())
                        collectSources(item.toObject());
            }
            if (type == "error" || o.value("error").isObject() || o.value("error").isString()) {
                const auto detail = o.value("error");
                state->error =
                    serviceError(r.config.provider, 200,
                                 detail.isObject() || detail.isString() ? detail : QJsonValue(o), key);
                state->terminal = true;
                continue;
            }
            if (r.config.provider == "DeepSeek" && r.config.accountLogin) {
                auto delta = o["delta"].toObject();
                if (type == "content_block_delta" && delta["type"] == "text_delta")
                    emit textDelta(r.id, delta["text"].toString());
                if (type == "message_delta" && delta["stop_reason"].isString())
                    state->stopReason = delta["stop_reason"].toString();
                if (type == "message_stop") {
                    state->terminal = true;
                    state->ok = state->error.isEmpty() &&
                                (state->stopReason == "end_turn" || state->stopReason == "stop_sequence");
                    if (!state->ok && state->error.isEmpty())
                        state->error = QStringLiteral("DeepSeek 输出未完整结束：") + state->stopReason;
                }
            } else if (r.config.provider == "DeepSeek") {
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
    connect(reply, &QNetworkReply::finished, this, [this, reply, r, state, consume, key, retry, origin] {
        consume();
        replies.remove(r.id);
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        // Never replay a possibly accepted POST. A fresh connection that closed
        // before TLS and before requestSent is the only retryable inference case.
        // encrypted() is not guaranteed on pooled connections, so an origin that
        // has ever completed TLS on this manager is conservatively excluded.
        const bool beforeTls = state->connecting && !state->encrypted && !state->sent && !state->sslError &&
                               !establishedTlsOrigins.contains(origin) &&
                               reply->sslConfiguration().sessionCipher().isNull() &&
                               !reply->attribute(QNetworkRequest::ConnectionEncryptedAttribute).toBool();
        if (r.config.provider == "DeepSeek" && !status && state->bytes == 0 && state->error.isEmpty() &&
            beforeTls && reply->error() == QNetworkReply::RemoteHostClosedError && retry < 2) {
            pendingRetry.insert(r.id);
            reply->deleteLater();
            QTimer::singleShot(retry == 0 ? 250 : 750, this, [this, r, key, retry] {
                if (!pendingRetry.remove(r.id))
                    return;
                if (r.config.accountLogin && (!account || account->sessionId() != r.config.accountId)) {
                    profileRequests.remove(r.id);
                    emit failed(r.id, QStringLiteral("DeepSeek 账号已更改，本次连接重试已停止"));
                    return;
                }
                begin(r, key, retry + 1);
            });
            return;
        }
        profileRequests.remove(r.id);
        if (status != 200) {
            auto body = QJsonDocument::fromJson(state->errorBody).object();
            QJsonValue detail = body.value("error");
            if (detail.isUndefined() || detail.isNull())
                detail = body;
            if (!status && detail.toObject().isEmpty())
                detail = QJsonObject{{"code", "network_error"}, {"message", reply->errorString()}};
            auto error = serviceError(r.config.provider, status, detail, key);
            if (retry)
                error += QStringLiteral("（已自动重试 %1 次）").arg(retry);
            emit failed(r.id, error);
        } else if (!state->error.isEmpty())
            emit failed(r.id, state->error);
        else if (reply->error() != QNetworkReply::NoError)
            emit failed(r.id, QStringLiteral("网络中断、超时或请求已取消"));
        else if (!state->terminal || !state->ok)
            emit failed(r.id,
                        state->error.isEmpty() ? QStringLiteral("响应中断，已保留接收内容") : state->error);
        else if (r.operation == "company_research" && (!state->searched || state->sources.isEmpty()))
            emit failed(
                r.id,
                QStringLiteral(
                    "联网检索未返回已完成的搜索与可用来源，未标记为已核验；可重试或取消联网仅分析填写资料"));
        else {
            QPointer<TextClient> guard(this);
            if (r.operation == "company_research")
                emit webSources(r.id, state->sources);
            if (!guard)
                return;
            emit finished(r.id);
        }
        if (status == 401 && r.config.provider == "DeepSeek" && r.config.accountLogin && account)
            account->rejectToken(r.config.accountId);
        reply->deleteLater();
    });
    return r.id;
}
void TextClient::cancel(const QString &id) {
    if (pendingRetry.remove(id)) {
        profileRequests.remove(id);
        emit failed(id, QStringLiteral("用户已取消连接重试"));
        return;
    }
    if (pendingAuth.remove(id)) {
        profileRequests.remove(id);
        emit failed(id, QStringLiteral("用户已取消等待授权"));
        return;
    }
    if (replies.contains(id))
        replies[id]->abort();
}
