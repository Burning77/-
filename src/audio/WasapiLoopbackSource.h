#pragma once
#include "AudioSource.h"
#include <QAudioFormat>
#include <QList>
#include <QThread>
struct RenderDevice {
    QString id, name;
};
class WasapiLoopbackSource : public AudioSource {
    Q_OBJECT
  public:
    explicit WasapiLoopbackSource(QString deviceId, QObject *p = nullptr)
        : AudioSource(p), deviceId(deviceId) {}
    ~WasapiLoopbackSource() {
        requestInterruption();
        wait();
    }
    static QList<RenderDevice> devices();

  protected:
    void run() override;

  private:
    QString deviceId;
};
