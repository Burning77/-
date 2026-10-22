#pragma once
#include <QJsonObject>
namespace SecretStore {
QJsonObject read(const QString &path);
bool write(const QString &path, const QJsonObject &value);
} // namespace SecretStore
