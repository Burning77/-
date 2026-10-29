#pragma once
#include "SelectionCopy.h"
#include "app/GlobalHotkey.h"
#include "app/WindowCapture.h"
#include <QImage>
#include <QPointer>
#include <QWidget>
class RegionSelector;
class QScreen;
using RegionGrabber = std::function<QImage(QScreen *, QRect)>;
class WrittenInput : public QObject {
    Q_OBJECT
  public:
    explicit WrittenInput(QWidget *owner, HotkeyBackend keys = {}, SelectionBackend selection = {},
                          WindowCaptureBackend protection = {}, RegionGrabber grabber = {});
    ~WrittenInput() override;
    void start();
    void stop();
    void captureSelection();
    void captureRegion();
  signals:
    void shortcutStatus(QString text, bool ready);
    void questionCaptured(QString text);
    void imageCaptured(QImage image);
    void status(QString message);

  private:
    QPointer<QWidget> owner;
    GlobalHotkey copyKey, regionKey;
    SelectionCopy selection;
    WindowCaptureBackend protection;
    RegionGrabber grabber;
    QPointer<RegionSelector> selector;
    bool running = false;
    quint64 serial = 0;
    void cancelCapture();
};
