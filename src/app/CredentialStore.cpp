#include "CredentialStore.h"
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincred.h>
#endif
bool CredentialStore::write(const QString &id, const QString &key) {
#ifdef Q_OS_WIN
    auto target = id.toStdWString();
    auto bytes = key.toUtf8();
    CREDENTIALW c{};
    c.Type = CRED_TYPE_GENERIC;
    c.TargetName = target.data();
    c.CredentialBlobSize = DWORD(bytes.size());
    c.CredentialBlob = reinterpret_cast<LPBYTE>(bytes.data());
    c.Persist = CRED_PERSIST_LOCAL_MACHINE;
    return CredWriteW(&c, 0) != FALSE;
#else
    return false;
#endif
}
QString CredentialStore::read(const QString &id) {
#ifdef Q_OS_WIN
    PCREDENTIALW c = nullptr;
    auto target = id.toStdWString();
    if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &c))
        return {};
    auto key = QString::fromUtf8(reinterpret_cast<char *>(c->CredentialBlob), int(c->CredentialBlobSize));
    CredFree(c);
    return key;
#else
    return {};
#endif
}
bool CredentialStore::remove(const QString &id) {
#ifdef Q_OS_WIN
    return CredDeleteW(id.toStdWString().c_str(), CRED_TYPE_GENERIC, 0) || GetLastError() == ERROR_NOT_FOUND;
#else
    return false;
#endif
}
