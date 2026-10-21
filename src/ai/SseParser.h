#pragma once
#include <QByteArray>
#include <QList>
#include <QString>
struct SseEvent {
    QString event, data;
};
class SseParser {
  public:
    QList<SseEvent> feed(const QByteArray &bytes);

  private:
    QByteArray buffer;
    QString type, data;
    bool hasData = false;
};
