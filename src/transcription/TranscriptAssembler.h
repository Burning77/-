#pragma once
#include <QHash>
#include <QJsonObject>
class TranscriptAssembler {
  public:
    QJsonObject apply(const QJsonObject &event, const QString &sessionId, const QString &channel,
                      qint64 startMs, qint64 endMs);

  private:
    QHash<QString, QJsonObject> items;
};
