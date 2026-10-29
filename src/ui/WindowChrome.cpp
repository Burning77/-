#include "WindowChrome.h"
#include <QEvent>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QWindow>

namespace {
class ResizeHandle : public QWidget {
    QPointer<QWidget> target;
    Qt::Edges edges;
    QRect initial;
    QPoint press;
    bool dragging = false;

  public:
    ResizeHandle(QWidget *window, Qt::Edges edges, const QString &name)
        : QWidget(window), target(window), edges(edges) {
        setObjectName(name);
        setCursor(edges == (Qt::LeftEdge | Qt::TopEdge) || edges == (Qt::RightEdge | Qt::BottomEdge)
                      ? Qt::SizeFDiagCursor
                  : edges == (Qt::RightEdge | Qt::TopEdge) || edges == (Qt::LeftEdge | Qt::BottomEdge)
                      ? Qt::SizeBDiagCursor
                  : edges.testFlag(Qt::LeftEdge) || edges.testFlag(Qt::RightEdge) ? Qt::SizeHorCursor
                                                                                  : Qt::SizeVerCursor);
    }

  protected:
    void mousePressEvent(QMouseEvent *event) override {
        if (!target || event->button() != Qt::LeftButton || target->isMaximized())
            return;
        if (target->windowHandle() && target->windowHandle()->startSystemResize(edges)) {
            event->accept();
            return;
        }
        press = event->globalPosition().toPoint();
        initial = target->geometry();
        dragging = true;
        event->accept();
    }
    void mouseMoveEvent(QMouseEvent *event) override {
        if (!target || !dragging || !event->buttons().testFlag(Qt::LeftButton))
            return;
        const auto delta = event->globalPosition().toPoint() - press;
        const auto minimum = target->minimumSize().expandedTo(target->minimumSizeHint());
        const auto maximum = target->maximumSize();
        auto geometry = initial;
        if (edges.testFlag(Qt::LeftEdge))
            geometry.setLeft(qBound(initial.right() - maximum.width() + 1, initial.left() + delta.x(),
                                    initial.right() - minimum.width() + 1));
        if (edges.testFlag(Qt::RightEdge))
            geometry.setRight(qBound(initial.left() + minimum.width() - 1, initial.right() + delta.x(),
                                     initial.left() + maximum.width() - 1));
        if (edges.testFlag(Qt::TopEdge))
            geometry.setTop(qBound(initial.bottom() - maximum.height() + 1, initial.top() + delta.y(),
                                   initial.bottom() - minimum.height() + 1));
        if (edges.testFlag(Qt::BottomEdge))
            geometry.setBottom(qBound(initial.top() + minimum.height() - 1, initial.bottom() + delta.y(),
                                      initial.top() + maximum.height() - 1));
        target->setGeometry(geometry);
        event->accept();
    }
    void mouseReleaseEvent(QMouseEvent *) override {
        dragging = false;
    }
};
} // namespace
WindowChrome::WindowChrome(QWidget *window) : QWidget(window), target(window) {
    setObjectName("windowTitleBar");
    setFixedHeight(34);
    auto layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 2, 8, 2);
    layout->setSpacing(4);
    title = new QLabel(window->windowTitle());
    title->setAttribute(Qt::WA_TransparentForMouseEvents);
    title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    layout->addWidget(title, 1);
    auto control = [&](const QString &name, const QString &text, const QString &description) {
        auto button = new QPushButton(text);
        button->setObjectName(name);
        button->setProperty("windowControl", true);
        button->setFixedSize(32, 28);
        button->setToolTip(description);
        button->setAccessibleName(description);
        layout->addWidget(button);
        return button;
    };
    auto minimize = control("windowMinimize", QStringLiteral("−"), QStringLiteral("最小化"));
    maximize = control("windowMaximize", QStringLiteral("□"), QStringLiteral("最大化/还原"));
    auto close = control("windowClose", QStringLiteral("×"), QStringLiteral("关闭"));
    connect(minimize, &QPushButton::clicked, window, &QWidget::showMinimized);
    connect(maximize, &QPushButton::clicked, window,
            [window] { window->isMaximized() ? window->showNormal() : window->showMaximized(); });
    connect(close, &QPushButton::clicked, window, &QWidget::close);
    for (const auto &edge : {Qt::Edges(Qt::TopEdge), Qt::Edges(Qt::RightEdge), Qt::Edges(Qt::BottomEdge),
                             Qt::Edges(Qt::LeftEdge), Qt::TopEdge | Qt::LeftEdge, Qt::TopEdge | Qt::RightEdge,
                             Qt::BottomEdge | Qt::RightEdge, Qt::BottomEdge | Qt::LeftEdge}) {
        const QStringList names{"resizeTop",     "resizeRight",    "resizeBottom",      "resizeLeft",
                                "resizeTopLeft", "resizeTopRight", "resizeBottomRight", "resizeBottomLeft"};
        handles.append(new ResizeHandle(window, edge, names[handles.size()]));
    }
    window->installEventFilter(this);
    sync();
}
void WindowChrome::sync() {
    if (!target)
        return;
    title->setText(target->windowTitle());
    maximize->setText(target->isMaximized() ? QStringLiteral("▣") : QStringLiteral("□"));
    const int w = target->width(), h = target->height(), border = 6;
    const QRect positions[] = {{border, 0, w - 2 * border, border},
                               {w - border, border, border, h - 2 * border},
                               {border, h - border, w - 2 * border, border},
                               {0, border, border, h - 2 * border},
                               {0, 0, border, border},
                               {w - border, 0, border, border},
                               {w - border, h - border, border, border},
                               {0, h - border, border, border}};
    for (qsizetype i = 0; i < handles.size(); ++i) {
        handles[i]->setGeometry(positions[i]);
        handles[i]->setVisible(!target->isMaximized() && !target->isFullScreen());
        handles[i]->raise();
    }
}
bool WindowChrome::eventFilter(QObject *, QEvent *event) {
    if (event->type() == QEvent::Resize || event->type() == QEvent::WindowStateChange ||
        event->type() == QEvent::WindowTitleChange || event->type() == QEvent::Show)
        sync();
    return false;
}
void WindowChrome::mousePressEvent(QMouseEvent *event) {
    if (!target || event->button() != Qt::LeftButton)
        return;
    if (target->windowHandle() && target->windowHandle()->startSystemMove()) {
        event->accept();
        return;
    }
    dragging = !target->isMaximized();
    pressPosition = event->globalPosition().toPoint();
    initialPosition = target->pos();
    event->accept();
}
void WindowChrome::mouseMoveEvent(QMouseEvent *event) {
    if (target && dragging && event->buttons().testFlag(Qt::LeftButton))
        target->move(initialPosition + event->globalPosition().toPoint() - pressPosition);
}
void WindowChrome::mouseReleaseEvent(QMouseEvent *) {
    dragging = false;
}
void WindowChrome::mouseDoubleClickEvent(QMouseEvent *event) {
    if (target && event->button() == Qt::LeftButton)
        maximize->click();
}
