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
    MainWindow window(nullptr, profile.path() + "/data");
    window.setWindowFlag(Qt::WindowDoesNotAcceptFocus);
    PaintCounter paints;
    window.installEventFilter(&paints);
    if (!cloak(window))
        return 3; // Abort before showing if desktop suppression fails.
    window.show();
    pump();
    const auto handle = window.winId();
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
    if (!check(window, 0x11) || !status->text().contains(QString::fromUtf8("已设置")))
        return 6;
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
    SettingsDialog dialog(&window);
    if (!cloak(dialog))
        return 10;
    dialog.show();
    pump();
    if (!check(dialog, 0x11))
        return 11;
    privacy->click();
    pump();
    if (!check(window, 0) || !check(dialog, 0))
        return 12;
    QTextStream(stdout) << "Painted native Qt MainWindow: " << paints.count
                        << " paints, layered HWND, affinity0x11 across normal/glass/maximize/restore; owned "
                           "settings0x11; both clear0. DWM cloak prevented desktop display."
                        << Qt::endl;
    return 0;
}
