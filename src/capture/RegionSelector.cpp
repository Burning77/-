#include "RegionSelector.h"
#include <QCloseEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
RegionSelector::RegionSelector(QRect area, QWidget *owner, WindowCaptureBackend backend)
    : QWidget(owner, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint), screenArea(area),
      protect(backend ? std::move(backend) : WindowCapture::setExcluded) {
    setObjectName("writtenRegionSelector");
    setProperty("captureExclusionIndependent", true);
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    // Keep the normal system cursor. All selection feedback is drawn inside
    // this protected HWND, never in a separate rubber-band/system window.
    setCursor(Qt::ArrowCursor);
}
bool RegionSelector::start(QString *error) {
    setGeometry(screenArea);
    const QPointer<RegionSelector> self(this);
    const auto result = protect(quintptr(winId()), true);
    if (!self)
        return false;
    if (!result.ok) {
        if (error)
            *error = QStringLiteral("无法保护截图选框，已取消截图：%1").arg(result.error);
        return false;
    }
    setProperty("captureSelectionActive", true);
    show();
    if (!self)
        return false;
    raise();
    activateWindow();
    setFocus();
    return true;
}
void RegionSelector::closeEvent(QCloseEvent *event) {
    cancelSelection();
    event->accept();
}
void RegionSelector::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor(0, 0, 0, 45));
    painter.setPen(Qt::white);
    painter.drawText(QPoint(18, 28), QStringLiteral("拖动选择题目区域；Esc取消"));
    if (dragging) {
        const auto selected = QRect(anchor, cursor).normalized().intersected(rect());
        painter.setPen(QPen(QColor("#60a5fa"), 2));
        painter.drawRect(selected.adjusted(0, 0, -1, -1));
        painter.drawText(selected.topLeft() + QPoint(4, 18),
                         QString("%1 × %2").arg(selected.width()).arg(selected.height()));
    }
}
void RegionSelector::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::RightButton) {
        cancelSelection();
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;
    anchor = cursor = event->position().toPoint();
    dragging = true;
    update();
}
void RegionSelector::mouseMoveEvent(QMouseEvent *event) {
    if (!dragging)
        return;
    cursor = event->position().toPoint();
    update();
}
void RegionSelector::mouseReleaseEvent(QMouseEvent *event) {
    if (completed || !dragging || event->button() != Qt::LeftButton)
        return;
    const auto selection = QRect(anchor, event->position().toPoint()).normalized().intersected(rect());
    if (selection.width() < 8 || selection.height() < 8) {
        cancelSelection();
        return;
    }
    completed = true;
    setProperty("captureSelectionActive", false);
    hide();
    emit selected(selection.translated(screenArea.topLeft()));
}
void RegionSelector::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape)
        cancelSelection();
    else
        QWidget::keyPressEvent(event);
}
void RegionSelector::cancelSelection() {
    if (completed)
        return;
    completed = true;
    setProperty("captureSelectionActive", false);
    hide();
    emit cancelled();
}
