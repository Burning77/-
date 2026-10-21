#pragma once
#include "domain/Types.h"
#include <QSettings>
class AppSettings {
  public:
    ProviderConfig textConfig() const;
    ProviderConfig config(const QString &provider) const;
    void save(const ProviderConfig &);
    void select(const QString &provider);
    ProviderConfig transcription() const;
    void saveTranscription(const ProviderConfig &);
};
