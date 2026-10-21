#include "AppSettings.h"
ProviderConfig AppSettings::config(const QString &provider) const {
    QSettings s;
    ProviderConfig c;
    c.provider = provider;
    c.baseUrl = s.value(provider + "/url",
                        provider == "DeepSeek" ? "https://api.deepseek.com" : "https://api.openai.com/v1")
                    .toString();
    c.model = s.value(provider + "/model", provider == "DeepSeek" ? "deepseek-flash" : "").toString();
    return c;
}
ProviderConfig AppSettings::textConfig() const {
    QSettings s;
    return config(s.value("provider", "OpenAI").toString());
}
void AppSettings::save(const ProviderConfig &c) {
    QSettings s;
    s.setValue(c.provider + "/url", c.baseUrl.trimmed());
    s.setValue(c.provider + "/model", c.model.trimmed());
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
