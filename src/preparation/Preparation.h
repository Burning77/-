#pragma once
#include "domain/Types.h"
namespace Preparation {
QString candidateText(const QJsonObject &);
QString fingerprint(const QJsonObject &);
QJsonObject load(const QString &root);
bool save(const QString &root, const QJsonObject &, QString *error = nullptr);
TextRequest request(const QJsonObject &, const ProviderConfig &);
QJsonObject parseResult(const QString &, QString *error = nullptr);
QString context(const QJsonObject &, int limit = 3000, const QString &focus = {});
QJsonObject correct(QJsonObject row, const QJsonObject &);
QJsonObject transcriptionOptions(QString model, const QJsonObject &);
} // namespace Preparation
