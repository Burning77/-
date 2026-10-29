#include "CapturePrivacy.h"
#include <QApplication>
#include <QEvent>
#include <QTimer>

CapturePrivacy::CapturePrivacy(QWidget *root, WindowCaptureBackend backend)
    : QObject(root), root(root), backend(backend ? std::move(backend) : WindowCapture::setExcluded) {
    setObjectName("capturePrivacy");
    track(root);
    qApp->installEventFilter(this);
}
CapturePrivacy::~CapturePrivacy() {
    if (qApp)
        qApp->removeEventFilter(this);
    for (const auto &state : windows)
        if (state.widget)
            disconnect(state.widget, nullptr, this, nullptr);
    // Keep the protection until the native windows themselves disappear.
    // Clearing it during owner destruction can expose a closing frame.
}
bool CapturePrivacy::owns(QWidget *widget) const {
    if (!widget || !root || !widget->isWindow())
        return false;
    // Screenshot selectors enforce their own verified exclusion even when
    // ordinary window privacy is off. Never clear that independent request.
    if (widget->property("captureExclusionIndependent").toBool())
        return false;
    for (auto parent = widget; parent; parent = parent->parentWidget())
        if (parent == root)
            return true;
    // Qt may keep its shared tooltip/popup container without a widget parent.
    // These are still this application's windows; other ordinary roots are not.
    return widget->windowType() == Qt::Popup || widget->windowType() == Qt::ToolTip;
}
void CapturePrivacy::track(QWidget *widget) {
    if (!widget || windows.contains(widget))
        return;
    State state;
    state.widget = widget;
    windows.insert(widget, state);
    connect(widget, &QObject::destroyed, this, [this, widget] {
        windows.remove(widget);
        emit stateChanged();
    });
}
void CapturePrivacy::apply(QWidget *widget, bool create) {
    if (!widget)
        return;
    if (applying) {
        if (applying != widget) {
            QPointer<QWidget> guard(widget);
            QTimer::singleShot(0, this, [this, guard] {
                if (guard && owns(guard))
                    apply(guard, enabled);
            });
        }
        return;
    }
    QPointer<CapturePrivacy> self(this);
    QPointer<QWidget> guard(widget);
    applying = widget;
    auto id = quintptr(widget->internalWinId());
    if (!id && create && enabled)
        id = quintptr(widget->winId());
    if (!self)
        return;
    if (!guard) {
        applying.clear();
        return;
    }
    auto &state = windows[widget];
    if (state.id != id) {
        state.id = id;
        state.attempted = false;
        state.excluded = false;
        state.error.clear();
    }
    if (!id || (!enabled && !state.attempted)) {
        applying.clear();
        emit stateChanged();
        return;
    }
    const bool desired = enabled;
    const auto invoke = backend;
    const auto result = invoke(id, desired);
    if (!self)
        return;
    applying.clear();
    if (!guard || !windows.contains(widget)) {
        emit stateChanged();
        return;
    }
    auto &completed = windows[widget];
    completed.attempted = true;
    completed.error = result.ok ? QString() : result.error;
    if (result.ok)
        completed.excluded = desired;
    if (quintptr(guard->internalWinId()) != id || enabled != desired) {
        QTimer::singleShot(0, this, [this, guard] {
            if (guard)
                apply(guard, enabled);
        });
    }
    emit stateChanged();
}
void CapturePrivacy::setRequested(bool value) {
    enabled = value;
    for (auto widget : QApplication::topLevelWidgets())
        if (owns(widget))
            track(widget);
    const auto keys = windows.keys();
    for (auto widget : keys)
        if (windows.contains(widget) && windows[widget].widget)
            apply(widget, value && widget == root);
    emit stateChanged();
}
bool CapturePrivacy::requested() const {
    return enabled;
}
bool CapturePrivacy::fullyApplied() const {
    if (applying || (enabled && !root))
        return false;
    for (const auto &state : windows) {
        if (!state.widget)
            continue;
        if (enabled && state.widget != root && !state.widget->isVisible())
            continue;
        if (!state.error.isEmpty())
            return false;
        if (enabled && (!state.id || !state.attempted || !state.excluded ||
                        quintptr(state.widget->internalWinId()) != state.id))
            return false;
        if (!enabled && state.excluded)
            return false;
    }
    return true;
}
QString CapturePrivacy::error() const {
    for (const auto &state : windows)
        if (state.widget && (!enabled || state.widget == root || state.widget->isVisible()) &&
            !state.error.isEmpty())
            return state.error;
    return fullyApplied() ? QString() : QStringLiteral("窗口状态尚未完全应用，请重试");
}
bool CapturePrivacy::eventFilter(QObject *object, QEvent *event) {
    auto widget = qobject_cast<QWidget *>(object);
    if (!owns(widget))
        return false;
    if (event->type() == QEvent::Show || event->type() == QEvent::WinIdChange) {
        track(widget);
        apply(widget, event->type() == QEvent::Show);
    } else if (event->type() == QEvent::Hide) {
        QTimer::singleShot(0, this, [this] { emit stateChanged(); });
    }
    return false;
}
