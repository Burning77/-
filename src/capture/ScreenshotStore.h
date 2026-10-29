#pragma once
#include <QJsonObject>
namespace ScreenshotStore {
QJsonObject save(QString root, QString sessionId, QString questionId, QByteArray png, QString *error);
QByteArray read(QString root, QString sessionId, QString questionId, QJsonObject metadata, QString *error);
} // namespace ScreenshotStore
