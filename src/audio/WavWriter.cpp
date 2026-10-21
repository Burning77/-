#include "WavWriter.h"
#include <QDataStream>
bool WavWriter::open(const QString &path) {
    close();
    file.setFileName(path);
    count = 0;
    lastError.clear();
    if (!file.open(QIODevice::WriteOnly)) {
        lastError = file.errorString();
        return false;
    }
    output = &file;
    if (output->write(QByteArray(44, 0)) == 44)
        return true;
    lastError = file.errorString();
    file.close();
    output = nullptr;
    return false;
}
bool WavWriter::open(QIODevice *device) {
    close();
    count = 0;
    lastError.clear();
    if (!device || !device->isOpen() || !device->isWritable() || device->isSequential() || !device->seek(0) ||
        device->write(QByteArray(44, 0)) != 44) {
        lastError = QStringLiteral("无法初始化 WAV 文件");
        return false;
    }
    output = device;
    return true;
}
bool WavWriter::append(const QByteArray &bytes) {
    if (!output || count + bytes.size() > 0xffffff00ll) {
        lastError = QStringLiteral("WAV 文件未打开或超过长度上限");
        return false;
    }
    auto n = output->write(bytes);
    if (n != bytes.size()) {
        lastError = output->errorString();
        return false;
    }
    count += n;
    return true;
}
bool WavWriter::close() {
    if (!output)
        return true;
    bool ok = output->seek(0);
    QByteArray header;
    QDataStream d(&header, QIODevice::WriteOnly);
    d.setByteOrder(QDataStream::LittleEndian);
    d.writeRawData("RIFF", 4);
    d << quint32(36 + count);
    d.writeRawData("WAVEfmt ", 8);
    d << quint32(16) << quint16(1) << quint16(1) << quint32(24000) << quint32(48000) << quint16(2)
      << quint16(16);
    d.writeRawData("data", 4);
    d << quint32(count);
    ok = ok && d.status() == QDataStream::Ok && output->write(header) == header.size();
    if (output == &file) {
        if (!file.flush())
            ok = false;
        file.close();
    }
    if (!ok)
        lastError = QStringLiteral("WAV 文件头或缓冲数据保存失败");
    output = nullptr;
    return ok;
}
