#include "BossVisibility.h"
#include <QApplication>
#include <QGuiApplication>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

BossVisibility::BossVisibility(QWidget *window) : QObject(window), root(window), primary(window) {
    qApp->installNativeEventFilter(this);
}
void BossVisibility::setPrimaryWindow(QWidget *window) {
    if (window && owns(window)) {
        primary = window;
        hiding = false;
        windows.clear();
        active.clear();
    }
}
BossVisibility::~BossVisibility() {
    if (qApp)
        qApp->removeNativeEventFilter(this);
}
bool BossVisibility::owns(QWidget *window) const {
    for (auto widget = window; widget; widget = widget->parentWidget())
        if (widget == root)
            return true;
    return false;
}
void BossVisibility::toggle() {
    if (!root || !primary)
        return;
    const QPointer<BossVisibility> self(this);
#ifdef Q_OS_WIN
    const bool native = QGuiApplication::platformName() == "windows";
    const auto rootHandle = reinterpret_cast<HWND>(primary->internalWinId());
    const bool minimized = native ? IsIconic(rootHandle) : primary->isMinimized();
#else
    const bool minimized = primary->isMinimized();
#endif
    if (!hiding && !minimized) {
        active = QApplication::activeModalWidget();
        if (!active || !owns(active))
            active = QApplication::activeWindow();
        windows.clear();
        for (auto window : QApplication::topLevelWidgets()) {
            if (!owns(window))
                continue;
#ifdef Q_OS_WIN
            const bool visible = native ? IsWindowVisible(reinterpret_cast<HWND>(window->internalWinId()))
                                        : window->isVisible();
#else
            const bool visible = window->isVisible();
#endif
            if (visible)
                windows.append(window);
        }
        hiding = true;
        const auto snapshot = windows;
        for (const auto &window : snapshot) {
            if (!window)
                continue;
#ifdef Q_OS_WIN
            if (native)
                ShowWindow(reinterpret_cast<HWND>(window->internalWinId()), SW_HIDE);
            else
#endif
                window->hide();
            if (!self)
                return;
        }
        return;
    }
    hiding = false;
    const auto snapshot = windows;
    windows.clear();
    // Show the owner before its dialogs. Native hiding preserves Qt's modal
    // event loop and unsaved fields; QWidget::hide() would end QDialog::exec().
#ifdef Q_OS_WIN
    if (native)
        ShowWindow(rootHandle, minimized ? SW_RESTORE : SW_SHOWNA);
    else
#endif
    {
        if (primary->isMinimized())
            primary->setWindowState(primary->windowState() & ~Qt::WindowMinimized);
        primary->show();
    }
    if (!self || !root)
        return;
    for (const auto &window : snapshot) {
        if (!window || window == primary || !owns(window))
            continue;
#ifdef Q_OS_WIN
        if (native)
            ShowWindow(reinterpret_cast<HWND>(window->internalWinId()), SW_SHOWNA);
        else
#endif
            window->show();
        if (!self)
            return;
    }
    auto target = QPointer<QWidget>(QApplication::activeModalWidget());
    if (!target || !owns(target))
        target = active && owns(active) ? active : primary;
    if (target) {
        target->raise();
        target->activateWindow();
    }
    emit restored();
}
bool BossVisibility::nativeEventFilter(const QByteArray &type, void *message, qintptr *) {
#ifdef Q_OS_WIN
    if (hiding && type == "windows_generic_MSG" && message) {
        const auto event = static_cast<MSG *>(message);
        if (event->message == WM_WINDOWPOSCHANGING) {
            auto position = reinterpret_cast<WINDOWPOS *>(event->lParam);
            auto widget = QWidget::find(WId(event->hwnd));
            // An explicit screenshot action may show only its independently
            // protected selector while the interview windows remain hidden.
            const bool captureActive = widget && widget->property("captureExclusionIndependent").toBool() &&
                                       widget->property("captureSelectionActive").toBool();
            if (position && (position->flags & SWP_SHOWWINDOW) && widget && widget->isWindow() &&
                owns(widget) && !captureActive) {
                if (!windows.contains(widget))
                    windows.append(widget);
                position->flags &= ~SWP_SHOWWINDOW;
                position->flags |= SWP_HIDEWINDOW;
            }
        }
    }
#else
    Q_UNUSED(type);
    Q_UNUSED(message);
#endif
    return false;
}
