#pragma once
#include <QJsonObject>
#include <QList>
#include <QSqlDatabase>
class SessionStore {
  public:
    SessionStore();
    ~SessionStore();
    bool open(const QString &root);
    bool put(const QString &kind, const QJsonObject &record);
    QList<QJsonObject> list(const QString &kind, const QString &sessionId = {}) const;
    bool removeSession(const QString &sessionId);
    QString root() const {
        return m_root;
    }
    QString lastError() const {
        return m_error;
    }

  private:
    QString m_root, m_connection;
    mutable QString m_error;
    QSqlDatabase m_db;
};
