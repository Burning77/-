#include "app/GlobalHotkey.h"
#include "app/NetworkRuntime.h"
#include "capture/RegionSelector.h"
#include "ui/BossVisibility.h"
#include "ui/MainWindow.h"
#include "ui/SettingsDialog.h"
#include "ui/WrittenInterviewWindow.h"
#include <QApplication>
#include <QDir>
#include <QEvent>
#include <QEventLoop>
#include <QMouseEvent>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <dwmapi.h>
#include <windows.h>
// Every additional test window is cloaked before its native show commits.
// No test reads desktop pixels or injects physical input.
class TestWindowConcealer : public QObject, public QAbstractNativeEventFilter {
  public:
    bool ok = true;
    bool conceal(HWND handle) {
        BOOL enabled = TRUE;
        DWORD cloaked = 0;
        const bool success =
            SUCCEEDED(DwmSetWindowAttribute(handle, DWMWA_CLOAK, &enabled, sizeof(enabled))) &&
            SUCCEEDED(DwmGetWindowAttribute(handle, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked;
        ok &= success;
        return success;
    }
    bool eventFilter(QObject *object, QEvent *event) override {
        auto widget = qobject_cast<QWidget *>(object);
        if (!widget || !widget->isWindow())
            return false;
        if (event->type() == QEvent::Polish) {
            widget->setAttribute(Qt::WA_ShowWithoutActivating);
            if (!widget->windowFlags().testFlag(Qt::WindowDoesNotAcceptFocus))
                widget->setWindowFlag(Qt::WindowDoesNotAcceptFocus);
        }
        if (event->type() == QEvent::WinIdChange && widget->internalWinId())
            conceal(reinterpret_cast<HWND>(widget->internalWinId()));
        return false;
    }
    bool nativeEventFilter(const QByteArray &type, void *message, qintptr *) override {
        if (type != "windows_generic_MSG" || !message)
            return false;
        const auto event = static_cast<MSG *>(message);
        if (event->message != WM_WINDOWPOSCHANGING)
            return false;
        auto position = reinterpret_cast<WINDOWPOS *>(event->lParam);
        auto widget = QWidget::find(WId(event->hwnd));
        if (position && (position->flags & SWP_SHOWWINDOW) && widget && widget->isWindow()) {
            position->flags |= SWP_NOACTIVATE;
            if (!conceal(event->hwnd)) {
                position->flags &= ~SWP_SHOWWINDOW;
                position->flags |= SWP_HIDEWINDOW;
            }
        }
        return false;
    }
};
class PaintCounter : public QObject {
  public:
    int count = 0;
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() == QEvent::Paint)
            ++count;
        return false;
    }
};
class SmokeWindow : public MainWindow {
  public:
    using MainWindow::MainWindow;
    int systemDragRequests = 0;
    void queueTaskbarRefresh() {
        MSG message{};
        message.hwnd = reinterpret_cast<HWND>(winId());
        message.message = WindowCapture::taskbarButtonCreatedMessage();
        qintptr result = 0;
        nativeEvent("windows_generic_MSG", &message, &result);
    }
    void destroyNativeSurface() {
        destroy();
    }

  protected:
    bool nativeEvent(const QByteArray &type, void *message, qintptr *result) override {
        if (type == "windows_generic_MSG") {
            const auto event = static_cast<MSG *>(message);
            const auto command = event->wParam & 0xfff0;
            const bool dragCommand =
                event->message == WM_SYSCOMMAND && (command == SC_MOVE || command == SC_SIZE);
            const bool dragHit =
                event->message == WM_NCLBUTTONDOWN &&
                (event->wParam == HTCAPTION || (event->wParam >= HTLEFT && event->wParam <= HTBOTTOMRIGHT));
            if (dragCommand || dragHit) {
                ++systemDragRequests;
                *result = 0;
                return true; // Never enter an OS move loop or draw desktop feedback in
                             // this fixture.
            }
        }
        return MainWindow::nativeEvent(type, message, result);
    }
};
static void drag(QWidget *widget, QPoint delta) {
    const auto press = widget->mapToGlobal(QPoint(qMin(80, widget->width() / 2), widget->height() / 2));
    for (auto type : {QEvent::MouseButtonPress, QEvent::MouseMove, QEvent::MouseButtonRelease}) {
        const auto global = type == QEvent::MouseButtonPress ? press : press + delta;
        QMouseEvent event(type, widget->mapFromGlobal(global), global,
                          type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                          type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(widget, &event);
    }
}
static void pump() {
    QEventLoop loop;
    QTimer::singleShot(150, &loop, &QEventLoop::quit);
    loop.exec();
}
static bool cloak(QWidget &widget) {
    widget.setAttribute(Qt::WA_ShowWithoutActivating);
    const auto handle = reinterpret_cast<HWND>(widget.winId());
    BOOL enabled = TRUE;
    if (FAILED(DwmSetWindowAttribute(handle, DWMWA_CLOAK, &enabled, sizeof(enabled))))
        return false;
    DWORD actual = 0;
    return SUCCEEDED(DwmGetWindowAttribute(handle, DWMWA_CLOAKED, &actual, sizeof(actual))) && actual;
}
static bool check(QWidget &widget, DWORD expected) {
    const auto handle = reinterpret_cast<HWND>(widget.winId());
    DWORD affinity = 0, cloaked = 0;
    return widget.isVisible() && IsWindowVisible(handle) &&
           SUCCEEDED(DwmGetWindowAttribute(handle, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked &&
           GetWindowDisplayAffinity(handle, &affinity) && affinity == expected;
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    const bool simulatedKeys = app.arguments().contains("--simulate-hotkeys");
    QHash<int, QPair<quint32, quint32>> fixtureKeys;
    HotkeyBackend hotkeys;
    if (simulatedKeys)
        hotkeys = [&](int id, quint32 modifiers, quint32 key, bool enabled) {
            if (!enabled) {
                fixtureKeys.remove(id);
                return HotkeyResult{true, {}};
            }
            const auto combo = qMakePair(modifiers, key);
            if (fixtureKeys.values().contains(combo))
                return HotkeyResult{false, "fixture conflict"};
            fixtureKeys[id] = combo;
            return HotkeyResult{true, {}};
        };
    TestWindowConcealer safety;
    app.installEventFilter(&safety);
    app.installNativeEventFilter(&safety);
    QTemporaryDir profile(QDir::currentPath() + "/native-main-XXXXXX");
    if (!profile.isValid())
        return 2;
    app.setOrganizationName("InterviewNativeCaptureTests");
    app.setApplicationName("IsolatedMainWindow");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile.path());
    QStandardPaths::setTestModeEnabled(true);
    QSettings().setValue("shortcuts/bossKey", "Ctrl+Alt+Shift+F11");
    QSettings().setValue("shortcuts/selectionCopy", "Ctrl+Alt+Shift+F8");
    QSettings().setValue("shortcuts/regionCapture", "Ctrl+Alt+Shift+F9");
    for (const auto &key : {"OpenAI/url", "DeepSeek/url", "transcription/url"})
        QSettings().setValue(key, "https://native-capture-fixture.invalid");
    NetworkRuntime::initializeTls();
    int taskbarCalls = 0;
    bool lastTaskbarOk = false;
    SmokeWindow window(
        nullptr, profile.path() + "/data", {},
        [&](quintptr id, bool on) {
            ++taskbarCalls;
            const auto result = WindowCapture::setTaskbarExcluded(id, on);
            lastTaskbarOk = result.ok;
            return result;
        },
        hotkeys);
    window.setWindowFlag(Qt::WindowDoesNotAcceptFocus);
    PaintCounter paints;
    window.installEventFilter(&paints);
    if (!cloak(window))
        return 3; // Abort before showing if desktop suppression fails.
    window.show();
    pump();
    const auto handle = window.winId();
    const auto originalShellStyle =
        GetWindowLongPtrW(reinterpret_cast<HWND>(handle), GWL_EXSTYLE) & (WS_EX_APPWINDOW | WS_EX_TOOLWINDOW);
    if (paints.count == 0 ||
        !(GetWindowLongPtrW(reinterpret_cast<HWND>(handle), GWL_EXSTYLE) & WS_EX_LAYERED))
        return 4;
    auto privacy = window.findChild<QPushButton *>("capturePrivacyToggle");
    auto appearance = window.findChild<QCheckBox *>("transparentBackground");
    auto status = window.findChild<QLabel *>("capturePrivacyStatus");
    if (!privacy || !appearance || !status)
        return 5;
    privacy->click();
    pump();
    if (!check(window, 0x11) || !status->text().contains(QString::fromUtf8("已设置"))) {
        QTextStream(stderr) << status->text() << Qt::endl;
        return 6;
    }
    const auto previousCalls = taskbarCalls;
    PostMessageW(reinterpret_cast<HWND>(handle), WindowCapture::taskbarButtonCreatedMessage(), 0, 0);
    pump();
    if (taskbarCalls <= previousCalls || !check(window, 0x11))
        return 13;
    auto bar = window.findChild<QWidget *>("windowTitleBar");
    auto right = window.findChild<QWidget *>("resizeRight");
    if (!bar || !right)
        return 19;
    const auto origin = window.pos();
    drag(bar, QPoint(20, 15));
    pump();
    const auto oldWidth = window.width();
    drag(right, QPoint(20, 0));
    pump();
    if (window.systemDragRequests || window.pos() != origin + QPoint(20, 15) ||
        window.width() != oldWidth + 20 || !check(window, 0x11)) {
        QTextStream(stderr) << "Private drag leaked system requests=" << window.systemDragRequests
                            << Qt::endl;
        return 20;
    }
    for (bool mode : {true, false, true}) {
        appearance->setChecked(mode);
        window.update();
        pump();
        if (window.winId() != handle || !check(window, 0x11))
            return 7;
    }
    window.showMaximized();
    pump();
    if (!window.isMaximized() || !check(window, 0x11))
        return 8;
    window.showNormal();
    pump();
    if (window.isMaximized() || !check(window, 0x11))
        return 9;
    window.showMinimized();
    pump();
    if (!window.isMinimized())
        return 15;
    window.showNormal();
    pump();
    if (window.isMinimized() || !check(window, 0x11))
        return 16;
    window.findChild<QPushButton *>("writtenInterviewButton")->click();
    pump();
    QPointer<WrittenInterviewWindow> written = window.findChild<WrittenInterviewWindow *>();
    if (!written || !safety.ok || window.isVisible() || !check(*written, 0x11))
        return 30;
    auto boss = window.findChild<GlobalHotkey *>();
    if (!boss)
        return 31;
    boss->activated();
    pump();
    if (IsWindowVisible(reinterpret_cast<HWND>(written->winId())) ||
        IsWindowVisible(reinterpret_cast<HWND>(handle)))
        return 32;
    {
        RegionSelector selector(QRect(0, 0, 640, 480), written);
        if (!selector.start() || !check(selector, 0x11) ||
            IsWindowVisible(reinterpret_cast<HWND>(written->winId())) ||
            IsWindowVisible(reinterpret_cast<HWND>(handle)))
            return 40;
        QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QCoreApplication::sendEvent(&selector, &escape);
    }
    boss->activated();
    pump();
    if (!check(*written, 0x11) || IsWindowVisible(reinterpret_cast<HWND>(handle)))
        return 33;
    {
        RegionSelector selector(QRect(0, 0, 640, 480), written);
        PaintCounter regionPaints;
        selector.installEventFilter(&regionPaints);
        if (!selector.start() || !check(selector, 0x11) || !safety.ok)
            return 34;
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(40, 40), QPointF(40, 40), Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QMouseEvent move(QEvent::MouseMove, QPointF(240, 160), QPointF(240, 160), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&selector, &press);
        QCoreApplication::sendEvent(&selector, &move);
        pump();
        if (!regionPaints.count || !check(selector, 0x11))
            return 35;
        privacy->click();
        pump();
        if (!check(selector, 0x11) || !check(*written, 0))
            return 36;
        privacy->click();
        pump();
        QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QCoreApplication::sendEvent(&selector, &escape);
    }
    written->findChild<QPushButton *>("endWrittenInterview")->click();
    pump();
    if (written || !check(window, 0x11))
        return 37;
    if (simulatedKeys) {
        if (fixtureKeys.size() != 1)
            return 38;
    } else {
        if (!RegisterHotKey(nullptr, 0x4102, MOD_CONTROL | MOD_ALT | MOD_SHIFT, VK_F8))
            return 38;
        UnregisterHotKey(nullptr, 0x4102);
    }
    SettingsDialog dialog(&window);
    if (!cloak(dialog))
        return 10;
    dialog.show();
    pump();
    if (!check(dialog, 0x11))
        return 11;
    auto key = window.findChild<GlobalHotkey *>();
    auto visibility = window.findChild<BossVisibility *>();
    if (!key || !key->registered() || !visibility) {
        QTextStream(stderr) << window.statusBar()->currentMessage() << Qt::endl;
        return 21;
    }
    // The real adapter must own the configured Windows shortcut. A second
    // registration must fail, without intercepting or injecting physical input.
    if (!simulatedKeys && RegisterHotKey(nullptr, 0x4100, MOD_CONTROL | MOD_ALT | MOD_SHIFT, VK_F11)) {
        UnregisterHotKey(nullptr, 0x4100);
        return 22;
    }
    if (!simulatedKeys && GetLastError() != ERROR_HOTKEY_ALREADY_REGISTERED)
        return 23;
    window.showMaximized();
    pump();
    const auto maximizedRect = window.geometry();
    visibility->toggle();
    visibility->toggle();
    pump();
    if (!window.isMaximized() || !IsZoomed(reinterpret_cast<HWND>(handle)) ||
        window.geometry() != maximizedRect || !check(window, 0x11)) {
        QTextStream(stderr) << "Boss key failed to preserve maximized native state" << Qt::endl;
        return 28;
    }
    window.showNormal();
    pump();
    QDialog modal(&window), late(&window);
    QLineEdit draft("unsaved draft", &modal);
    if (!cloak(modal) || !cloak(late))
        return 24;
    bool hiddenCorrectly = false, restoredDuringExec = false;
    QTimer::singleShot(20, &modal, [&] {
        visibility->toggle();
        late.show(); // New owned dialogs must stay hidden as well.
        hiddenCorrectly = visibility->hidden() && !IsWindowVisible(reinterpret_cast<HWND>(handle)) &&
                          !IsWindowVisible(reinterpret_cast<HWND>(dialog.winId())) &&
                          !IsWindowVisible(reinterpret_cast<HWND>(modal.winId())) &&
                          !IsWindowVisible(reinterpret_cast<HWND>(late.winId()));
        QTimer::singleShot(40, &modal, [&] {
            visibility->toggle();
            restoredDuringExec = !visibility->hidden() && check(window, 0x11) && check(dialog, 0x11) &&
                                 check(modal, 0x11) && check(late, 0x11) && draft.text() == "unsaved draft";
            modal.accept();
        });
    });
    if (modal.exec() != QDialog::Accepted || !hiddenCorrectly || !restoredDuringExec) {
        QTextStream(stderr) << "Boss key modal round trip: hide=" << hiddenCorrectly
                            << " restore=" << restoredDuringExec << Qt::endl;
        return 25;
    }
    late.hide();
    // Destruction really releases native registration, so a new application
    // can claim this unused fixture shortcut afterward.
    {
        GlobalHotkey temporary(nullptr, hotkeys);
        if (!temporary.setSequence(QKeySequence("Ctrl+Alt+Shift+F10")).ok)
            return 26;
    }
    if (simulatedKeys) {
        if (fixtureKeys.size() != 1)
            return 27;
    } else {
        if (!RegisterHotKey(nullptr, 0x4101, MOD_CONTROL | MOD_ALT | MOD_SHIFT, VK_F10))
            return 27;
        UnregisterHotKey(nullptr, 0x4101);
    }
    window.queueTaskbarRefresh();
    privacy->click();
    const auto callsAfterDisable = taskbarCalls;
    pump();
    if (!check(window, 0) || !check(dialog, 0))
        return 12;
    if (privacy->isChecked() || taskbarCalls != callsAfterDisable)
        return 17;
    if ((GetWindowLongPtrW(reinterpret_cast<HWND>(handle), GWL_EXSTYLE) &
         (WS_EX_APPWINDOW | WS_EX_TOOLWINDOW)) != originalShellStyle)
        return 14;
    const auto callsBeforeDestroy = taskbarCalls;
    window.destroyNativeSurface();
    if (taskbarCalls != callsBeforeDestroy + 1 || !lastTaskbarOk)
        return 18;
    if (!safety.ok)
        return 39;
    QTextStream(stdout) << "Painted native Qt MainWindow: " << paints.count
                        << " paints, layered HWND, affinity0x11 across "
                           "normal/glass/maximize/restore; owned "
                           "settings0x11; both clear0. Shell DeleteTab/AddTab "
                           "succeeded; button recreation "
                           "reapplies privacy; native styles preserved. DWM "
                           "cloak prevented desktop display."
                        << (simulatedKeys ? " Hotkey registration SIMULATED; "
                                          : " Native hotkey registration verified; ")
                        << "hide/restore preserved modal exec, draft and WDA."
                           " Written-mode round trip, active boss target and "
                           "independent painted region exclusion passed."
                        << Qt::endl;
    return 0;
}
