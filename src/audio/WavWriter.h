#pragma once
#include <QFile>
class WavWriter {
  public:
    bool open(const QString &path);
    bool open(QIODevice *device);
    bool append(const QByteArray &bytes);
    virtual bool close();
    virtual qint64 size() const {
        return count;
    }
    QString error() const {
        return lastError;
    }
    virtual ~WavWriter() {
        close();
    }

  private:
    QFile file;
    QIODevice *output = nullptr;
    qint64 count = 0;
    QString lastError;
};
