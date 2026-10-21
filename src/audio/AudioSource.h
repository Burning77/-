#pragma once
#include <QThread>
#include <QAudioFormat>
class AudioSource : public QThread {
    Q_OBJECT
  public:
    using QThread::QThread;
  signals:
    void formatReady(QAudioFormat format);
    void pcm(QByteArray bytes);
    void sourceError(QString message);
};
