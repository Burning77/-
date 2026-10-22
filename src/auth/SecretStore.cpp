#include "SecretStore.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincrypt.h>
#endif
namespace SecretStore {
QJsonObject read(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024)
        return {};
    auto bytes = file.readAll();
#ifdef Q_OS_WIN
    DATA_BLOB input{DWORD(bytes.size()), reinterpret_cast<BYTE *>(bytes.data())}, output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output))
        return {};
    QByteArray plain(reinterpret_cast<char *>(output.pbData), output.cbData);
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    auto result = QJsonDocument::fromJson(plain).object();
    plain.fill(0);
    return result;
#else
    return {};
#endif
}
bool write(const QString &path, const QJsonObject &value) {
    auto plain = QJsonDocument(value).toJson(QJsonDocument::Compact);
    QByteArray encrypted;
#ifdef Q_OS_WIN
    DATA_BLOB input{DWORD(plain.size()), reinterpret_cast<BYTE *>(plain.data())}, output{};
    if (!CryptProtectData(&input, L"InterviewAssistant ChatGPT", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &output))
        return false;
    encrypted = QByteArray(reinterpret_cast<char *>(output.pbData), output.cbData);
    LocalFree(output.pbData);
    plain.fill(0);
#else
    return false;
#endif
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(encrypted) == encrypted.size() && file.commit();
}
} // namespace SecretStore
