#pragma once
#include <QFile>
class WavWriter {
  public:
    bool open(const QString &path);
    bool append(const QByteArray &bytes);
    void close();
    qint64 size() const {
        return count;
    }
    QString error() const {
        return file.errorString();
    }
    ~WavWriter() {
        close();
    }

  private:
    QFile file;
    qint64 count = 0;
};
