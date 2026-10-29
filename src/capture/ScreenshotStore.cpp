#include "ScreenshotStore.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QRegularExpression>
#include <QSaveFile>
namespace {
QString relativePath(QString sid, QString qid) {
    return "sessions/" + sid + "/questions/" + qid + ".png";
}
bool safePath(QString root, QString sid, QString qid, QString *error) {
    const QRegularExpression safe("^[A-Za-z0-9_-]{1,128}$");
    const auto base = QFileInfo(root).canonicalFilePath();
    if (base.isEmpty() || !safe.match(sid).hasMatch() || !safe.match(qid).hasMatch()) {
        if (error)
            *error = QStringLiteral("截图的会话或路径无效");
        return false;
    }
    for (const auto &path :
         {QString("sessions"), "sessions/" + sid, "sessions/" + sid + "/questions", relativePath(sid, qid)}) {
        const QFileInfo info(QDir(root).filePath(path));
        if (info.isSymLink() || (info.exists() && !info.canonicalFilePath().startsWith(base + "/"))) {
            if (error)
                *error = QStringLiteral("截图路径不安全，已取消读取或保存");
            return false;
        }
    }
    return true;
}
QString digest(const QByteArray &png) {
    return QString::fromLatin1(QCryptographicHash::hash(png, QCryptographicHash::Sha256).toHex());
}
} // namespace
QJsonObject ScreenshotStore::save(QString root, QString sid, QString qid, QByteArray png, QString *error) {
    if (error)
        error->clear();
    if (!safePath(root, sid, qid, error))
        return {};
    QBuffer source(&png);
    source.open(QIODevice::ReadOnly);
    QImageReader reader(&source, "PNG");
    const auto size = reader.size();
    if (png.isEmpty() || png.size() > 10 * 1024 * 1024 || !size.isValid() ||
        qint64(size.width()) * size.height() > 16000000) {
        if (error)
            *error = QStringLiteral("截图无效或过大，请缩小选区");
        return {};
    }
    const auto relative = relativePath(sid, qid), path = QDir(root).filePath(relative);
    if (!QDir().mkpath(QFileInfo(path).absolutePath()) || !safePath(root, sid, qid, error))
        return {};
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(png) != png.size() || !file.commit()) {
        if (error)
            *error = QStringLiteral("无法保存截图：%1").arg(file.errorString());
        return {};
    }
    return {{"path", relative}, {"sha256", digest(png)}, {"width", size.width()}, {"height", size.height()}};
}
QByteArray ScreenshotStore::read(QString root, QString sid, QString qid, QJsonObject metadata,
                                 QString *error) {
    if (error)
        error->clear();
    if (!safePath(root, sid, qid, error))
        return {};
    if (metadata["path"].toString() != relativePath(sid, qid)) {
        if (error)
            *error = QStringLiteral("截图不属于当前题目或会话");
        return {};
    }
    QFile file(QDir(root).filePath(relativePath(sid, qid)));
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > 10 * 1024 * 1024) {
        if (error)
            *error = QStringLiteral("截图文件缺失或过大，请重新截图");
        return {};
    }
    const auto png = file.readAll();
    if (digest(png) != metadata["sha256"].toString() ||
        !png.startsWith(QByteArray::fromHex("89504e470d0a1a0a"))) {
        if (error)
            *error = QStringLiteral("截图文件已更改，请重新截图");
        return {};
    }
    return png;
}
