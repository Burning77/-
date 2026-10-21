#pragma once
#include <QString>
class CredentialStore {
  public:
    static bool write(const QString &id, const QString &key);
    static QString read(const QString &id);
    static bool remove(const QString &id);
};
