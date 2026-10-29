#include "ui/MainWindow.h"
#include "ui/SettingsDialog.h"
#include "app/NetworkRuntime.h"
#include <QApplication>
#include <QDir>
#include <QEvent>
#include <QEventLoop>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
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
};
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
    QTemporaryDir profile(QDir::currentPath() + "/native-main-XXXXXX");
    if (!profile.isValid())
        return 2;
    app.setOrganizationName("InterviewNativeCaptureTests");
    app.setApplicationName("IsolatedMainWindow");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile.path());
    QStandardPaths::setTestModeEnabled(true);
    for (const auto &key : {"OpenAI/url", "DeepSeek/url", "transcription/url"})
        QSettings().setValue(key, "https://native-capture-fixture.invalid");
    NetworkRuntime::initializeTls();
    int taskbarCalls = 0;
    bool lastTaskbarOk = false;
    SmokeWindow window(nullptr, profile.path() + "/data", {}, [&](quintptr id, bool on) {
        ++taskbarCalls;
        const auto result = WindowCapture::setTaskbarExcluded(id, on);
        lastTaskbarOk = result.ok;
        return result;
    });
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
    SettingsDialog dialog(&window);
    if (!cloak(dialog))
        return 10;
    dialog.show();
    pump();
    if (!check(dialog, 0x11))
        return 11;
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
    QTextStream(stdout) << "Painted native Qt MainWindow: " << paints.count
                        << " paints, layered HWND, affinity0x11 across normal/glass/maximize/restore; owned "
                           "settings0x11; both clear0. Shell DeleteTab/AddTab succeeded; button recreation "
                           "reapplies privacy; native styles preserved. DWM cloak prevented desktop display."
                        << Qt::endl;
    return 0;
}
