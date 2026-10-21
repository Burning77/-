#pragma once
#include <QAudioFormat>
#include <QByteArray>
#include <QVector>
class PcmConverter {
  public:
    explicit PcmConverter(const QAudioFormat &format) : format(format) {}
    QByteArray process(const QByteArray &bytes);

  private:
    QAudioFormat format;
    QByteArray tail;
    QVector<float> samples;
    double position = 0;
};
