#include "WavWriter.h"
#include <QDataStream>
bool WavWriter::open(const QString &path) {
    close();
    file.setFileName(path);
    count = 0;
    if (!file.open(QIODevice::WriteOnly))
        return false;
    return file.write(QByteArray(44, 0)) == 44;
}
bool WavWriter::append(const QByteArray &bytes) {
    if (!file.isOpen() || count + bytes.size() > 0xffffff00ll)
        return false;
    auto n = file.write(bytes);
    if (n != bytes.size())
        return false;
    count += n;
    return true;
}
void WavWriter::close() {
    if (!file.isOpen())
        return;
    file.seek(0);
    QDataStream d(&file);
    d.setByteOrder(QDataStream::LittleEndian);
    d.writeRawData("RIFF", 4);
    d << quint32(36 + count);
    d.writeRawData("WAVEfmt ", 8);
    d << quint32(16) << quint16(1) << quint16(1) << quint32(24000) << quint32(48000) << quint16(2)
      << quint16(16);
    d.writeRawData("data", 4);
    d << quint32(count);
    file.flush();
    file.close();
}
