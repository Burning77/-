#pragma once
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QWidget>

class WindowChrome : public QWidget {
  public:
    explicit WindowChrome(QWidget *window);

  protected:
    bool eventFilter(QObject *, QEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;

  private:
    QPointer<QWidget> target;
    QLabel *title;
    QPushButton *maximize;
    QList<QWidget *> handles;
    QPoint pressPosition, initialPosition;
    bool dragging = false;
    void sync();
};
