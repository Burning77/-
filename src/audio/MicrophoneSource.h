#pragma once
#include "AudioSource.h"
#include <QAudioDevice>
#include <QAudioFormat>
#include <QThread>
class MicrophoneSource : public AudioSource {
    Q_OBJECT
  public:
    explicit MicrophoneSource(QAudioDevice device, QObject *p = nullptr) : AudioSource(p), device(device) {}
    ~MicrophoneSource() {
        requestInterruption();
        quit();
        wait();
    }

  protected:
    void run() override;

  private:
    QAudioDevice device;
};
