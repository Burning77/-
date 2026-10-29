#pragma once
#include "app/WindowCapture.h"
#include <QWidget>
class RegionSelector : public QWidget {
    Q_OBJECT
  public:
    explicit RegionSelector(QRect screenArea, QWidget *owner, WindowCaptureBackend protection = {});
    bool start(QString *error = nullptr);
  signals:
    void selected(QRect globalRegion);
    void cancelled();

  protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void closeEvent(QCloseEvent *) override;

  private:
    QRect screenArea;
    QPoint anchor, cursor;
    bool dragging = false, completed = false;
    WindowCaptureBackend protect;
    void cancelSelection();
};
