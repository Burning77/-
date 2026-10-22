#pragma once
#include <QAudioFormat>
#include <QByteArray>
#include <QVector>
class PcmConverter {
  public:
    explicit PcmConverter(const QAudioFormat &format, int outputRate = 24000)
        : format(format), outputRate(outputRate) {}
    QByteArray process(const QByteArray &bytes);

  private:
    QAudioFormat format;
    int outputRate;
    QByteArray tail;
    QVector<float> samples;
    double position = 0;
};
