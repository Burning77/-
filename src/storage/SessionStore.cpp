#include "SessionStore.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
static bool validKind(const QString &s) {
    return QStringList{"sessions", "transcripts", "questions", "generations", "reviews", "audio", "gaps"}
        .contains(s);
}
SessionStore::SessionStore() : m_connection(QUuid::createUuid().toString()) {}
SessionStore::~SessionStore() {
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connection);
}
bool SessionStore::open(const QString &root) {
    m_root = QDir(root).absolutePath();
    if (!QDir().mkpath(m_root + "/sessions")) {
        m_error = QStringLiteral("无法创建数据目录");
        return false;
    }
    m_db = QSqlDatabase::addDatabase("QSQLITE", m_connection);
    m_db.setDatabaseName(m_root + "/interviews.sqlite");
    if (!m_db.open()) {
        m_error = m_db.lastError().text();
        return false;
    }
    QSqlQuery q(m_db);
    if (!q.exec("CREATE TABLE IF NOT EXISTS records(kind TEXT NOT NULL,id TEXT NOT NULL,sessionId TEXT NOT "
                "NULL,data TEXT NOT NULL,PRIMARY KEY(kind,id))")) {
        m_error = q.lastError().text();
        return false;
    }
    q.exec("CREATE INDEX IF NOT EXISTS records_by_session ON records(sessionId,kind)");
    q.exec("PRAGMA user_version=1");
    return true;
}
bool SessionStore::put(const QString &kind, const QJsonObject &record) {
    QString id = record["id"].toString(), sid = kind == "sessions" ? id : record["sessionId"].toString();
    static QRegularExpression safe("^[A-Za-z0-9_-]{1,128}$");
    if (!validKind(kind) || !safe.match(id).hasMatch() || !safe.match(sid).hasMatch()) {
        m_error = "Invalid record identity";
        return false;
    }
    if (!m_db.transaction()) {
        m_error = m_db.lastError().text();
        return false;
    }
    QSqlQuery q(m_db);
    if (kind != "sessions") {
        q.prepare("SELECT id FROM records WHERE kind='sessions' AND id=?");
        q.addBindValue(sid);
        if (!q.exec() || !q.next()) {
            m_db.rollback();
            m_error = "Unknown session";
            return false;
        }
    }
    q.prepare("SELECT sessionId FROM records WHERE kind=? AND id=?");
    q.addBindValue(kind);
    q.addBindValue(id);
    if (!q.exec()) {
        m_db.rollback();
        m_error = q.lastError().text();
        return false;
    }
    if (q.next() && q.value(0).toString() != sid) {
        m_db.rollback();
        m_error = "Record belongs to another session";
        return false;
    }
    q.finish();
    q.prepare("INSERT INTO records(kind,id,sessionId,data) VALUES(?,?,?,?) ON CONFLICT(kind,id) DO UPDATE "
              "SET data=excluded.data");
    q.addBindValue(kind);
    q.addBindValue(id);
    q.addBindValue(sid);
    q.addBindValue(QString::fromUtf8(QJsonDocument(record).toJson(QJsonDocument::Compact)));
    if (!q.exec()) {
        m_error = q.lastError().text();
        m_db.rollback();
        return false;
    }
    if (!m_db.commit()) {
        m_error = m_db.lastError().text();
        return false;
    }
    return true;
}
QList<QJsonObject> SessionStore::list(const QString &kind, const QString &sid) const {
    QList<QJsonObject> rows;
    if (!validKind(kind))
        return rows;
    QSqlQuery q(m_db);
    q.prepare("SELECT data FROM records WHERE kind=?" + QString(sid.isEmpty() ? "" : " AND sessionId=?") +
              " ORDER BY rowid");
    q.addBindValue(kind);
    if (!sid.isEmpty())
        q.addBindValue(sid);
    if (!q.exec()) {
        m_error = q.lastError().text();
        return rows;
    }
    while (q.next())
        rows.append(QJsonDocument::fromJson(q.value(0).toString().toUtf8()).object());
    return rows;
}
bool SessionStore::removeSession(const QString &sid) {
    static QRegularExpression safe("^[A-Za-z0-9_-]{1,128}$");
    if (!safe.match(sid).hasMatch())
        return false;
    auto path = m_root + "/sessions/" + sid;
    QFileInfo info(path);
    if (info.exists() &&
        (info.isSymLink() ||
         !info.canonicalFilePath().startsWith(QFileInfo(m_root + "/sessions").canonicalFilePath() + "/"))) {
        m_error = "Unsafe session path";
        return false;
    }
    if (info.exists() && !QDir(path).removeRecursively()) {
        m_error = "Unable to remove audio files";
        return false;
    }
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM records WHERE sessionId=?");
    q.addBindValue(sid);
    if (!q.exec()) {
        m_error = q.lastError().text();
        return false;
    }
    return true;
}
