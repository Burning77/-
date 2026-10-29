#pragma once
#include "app/WindowCapture.h"
#include <QPointer>
#include <QWidget>
#include <QHash>

class CapturePrivacy : public QObject {
    Q_OBJECT
  public:
    explicit CapturePrivacy(QWidget *root, WindowCaptureBackend backend = {});
    ~CapturePrivacy() override;
    void setRequested(bool enabled);
    bool requested() const;
    bool fullyApplied() const;
    QString error() const;
  signals:
    void stateChanged();

  protected:
    bool eventFilter(QObject *, QEvent *) override;

  private:
    struct State {
        QPointer<QWidget> widget;
        quintptr id = 0;
        bool attempted = false;
        bool excluded = false;
        QString error;
    };
    QPointer<QWidget> root;
    WindowCaptureBackend backend;
    bool enabled = false;
    QHash<QWidget *, State> windows;
    QPointer<QWidget> applying;
    bool owns(QWidget *) const;
    void track(QWidget *);
    void apply(QWidget *, bool create);
};
