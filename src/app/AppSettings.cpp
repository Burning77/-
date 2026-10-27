#include "AppSettings.h"
#include "LocalModelPath.h"
#include "auth/DeepSeekAccount.h"
#include <QCoreApplication>
QString AppSettings::transcriptionMode() const {
    return QSettings().value("transcription/mode", "Local").toString();
}
QString AppSettings::localModelPath() const {
    return resolveLocalModelPath(QSettings().value("transcription/bilingualModel").toString(),
                                 QCoreApplication::applicationDirPath());
}
void AppSettings::saveLocalTranscription(QString mode, QString path) {
    QSettings s;
    s.setValue("transcription/mode", mode);
    s.setValue("transcription/bilingualModel", path);
}
ProviderConfig AppSettings::config(const QString &provider) const {
    QSettings s;
    ProviderConfig c;
    c.provider = provider;
    c.baseUrl = s.value(provider + "/url",
                        provider == "DeepSeek" ? "https://api.deepseek.com" : "https://api.openai.com/v1")
                    .toString();
    c.model = s.value(provider + "/model", provider == "DeepSeek" ? "deepseek-flash" : "").toString();
    if (provider == "ChatGPT") {
        c.baseUrl = "https://api.openai.com/v1";
        c.accountId = s.value("chatgpt/active").toString();
        c.reasoningEffort = s.value("ChatGPT/reasoningEffort", "high").toString();
        if (!QStringList{"", "low", "medium", "high", "xhigh", "max"}.contains(c.reasoningEffort))
            c.reasoningEffort = "high";
    }
    if (provider == "DeepSeek") {
        c.accountLogin = s.value("DeepSeek/accountLogin", false).toBool();
        if (c.accountLogin) {
            c.baseUrl = "https://api.deepseek.com";
            c.accountId = DeepSeekAccount::shared().sessionId();
        }
    }
    return c;
}
ProviderConfig AppSettings::textConfig() const {
    QSettings s;
    return config(s.value("provider", "OpenAI").toString());
}
void AppSettings::save(const ProviderConfig &c) {
    QSettings s;
    if (c.provider != "DeepSeek" || !c.accountLogin)
        s.setValue(c.provider + "/url", c.baseUrl.trimmed());
    if (c.provider == "DeepSeek")
        s.setValue("DeepSeek/accountLogin", c.accountLogin);
    s.setValue(c.provider + "/model", c.model.trimmed());
    if (c.provider == "ChatGPT")
        s.setValue("ChatGPT/reasoningEffort", c.reasoningEffort);
}
void AppSettings::select(const QString &p) {
    QSettings().setValue("provider", p);
}
ProviderConfig AppSettings::transcription() const {
    QSettings s;
    ProviderConfig c;
    c.provider = "Transcription";
    c.baseUrl =
        s.value("transcription/url", "wss://api.openai.com/v1/realtime?intent=transcription").toString();
    c.model = s.value("transcription/model", "gpt-live-transcribe").toString();
    return c;
}
void AppSettings::saveTranscription(const ProviderConfig &c) {
    QSettings s;
    s.setValue("transcription/url", c.baseUrl.trimmed());
    s.setValue("transcription/model", c.model.trimmed());
}
