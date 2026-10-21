#include "TextClient.h"
#include "SseParser.h"
#include "app/CredentialStore.h"
#include <QJsonDocument>
#include <QTimer>
#include <memory>
TextClient::TextClient(QObject *p) : QObject(p), manager(this) {
    keyReader = CredentialStore::read;
}
QJsonObject TextClient::payload(const TextRequest &r) {
    QJsonObject o{{"model", r.config.model}, {"stream", true}};
    if (r.config.provider == "DeepSeek") {
        o["messages"] = r.messages;
        o["thinking"] = QJsonObject{{"type", "disabled"}};
        if (r.jsonMode)
            o["response_format"] = QJsonObject{{"type", "json_object"}};
    } else {
        o["input"] = r.messages;
        if (r.jsonMode)
            o["text"] = QJsonObject{{"format", QJsonObject{{"type", "json_object"}}}};
    }
    return o;
}
QString TextClient::start(const TextRequest &r) {
    auto reject = [this, r](QString message) {
        QTimer::singleShot(0, this, [this, r, message] { emit failed(r.id, message); });
        return r.id;
    };
    QUrl url(r.config.baseUrl);
    bool local = url.host() == "127.0.0.1" || url.host() == "localhost" || url.host() == "::1";
    if (!url.isValid() || url.host().isEmpty() || (!local && url.scheme() != "https") ||
        !url.userInfo().isEmpty())
        return reject(QStringLiteral("API 地址须为有效的 HTTPS 地址"));
    if (r.config.model.trimmed().isEmpty())
        return reject(QStringLiteral("请在设置中填写模型名称"));
    auto key = keyReader(r.config.credentialId());
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
        qint64 bytes = 0;
    };
    auto state = std::make_shared<State>();
    auto consume = [this, reply, r, state] {
        auto bytes = reply->readAll();
        state->bytes += bytes.size();
        if (state->bytes > 8 * 1024 * 1024) {
            state->error = QStringLiteral("响应过大，已停止生成");
            reply->abort();
            return;
        }
        if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200)
            return;
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
            if (o.contains("error")) {
                state->error = QStringLiteral("模型服务返回错误，请检查模型、额度和 API 配置");
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
                    state->error = QStringLiteral("模型输出未完成，请重试");
                }
            }
        }
    };
    connect(reply, &QNetworkReply::readyRead, this, consume);
    connect(reply, &QNetworkReply::finished, this, [this, reply, r, state, consume] {
        consume();
        replies.remove(r.id);
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 200)
            emit failed(r.id, QStringLiteral("%1 请求失败（HTTP %2），请检查网络、模型和额度")
                                  .arg(r.config.provider)
                                  .arg(status));
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
    if (replies.contains(id))
        replies[id]->abort();
}
