#include "WindowCapture.h"
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#include <shobjidl.h>
#include <QScopeGuard>
#endif

bool WindowCapture::supportedVersion(int major, int build) {
    return major > 10 || (major == 10 && build >= 19041);
}
WindowCaptureResult WindowCapture::setTaskbarExcluded(quintptr window, bool excluded) {
#ifdef Q_OS_WIN
    const auto handle = reinterpret_cast<HWND>(window);
    DWORD process = 0;
    if (!handle || !IsWindow(handle))
        return {false, QStringLiteral("任务栏图标设置失败：窗口句柄无效")};
    GetWindowThreadProcessId(handle, &process);
    if (process != GetCurrentProcessId() || GetAncestor(handle, GA_ROOT) != handle ||
        (GetWindowLongPtrW(handle, GWL_STYLE) & WS_CHILD))
        return {false, QStringLiteral("只能设置本进程顶层窗口的任务栏图标")};
    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ITaskbarList *taskbar = nullptr;
    const auto cleanup = qScopeGuard([&] {
        if (taskbar)
            taskbar->Release();
        if (SUCCEEDED(initialized))
            CoUninitialize();
    });
    auto result = initialized;
    if (SUCCEEDED(initialized) || initialized == RPC_E_CHANGED_MODE)
        result = CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_ITaskbarList,
                                  reinterpret_cast<void **>(&taskbar));
    if (SUCCEEDED(result))
        result = taskbar->HrInit();
    if (SUCCEEDED(result))
        result = excluded ? taskbar->DeleteTab(handle) : taskbar->AddTab(handle);
    if (FAILED(result))
        return {false, QStringLiteral("任务栏图标%1失败（HRESULT 0x%2）")
                           .arg(excluded ? QStringLiteral("隐藏") : QStringLiteral("恢复"))
                           .arg(quint32(result), 8, 16, QChar('0'))};
    return {true, {}};
#else
    Q_UNUSED(window);
    Q_UNUSED(excluded);
    return {false, QStringLiteral("当前系统不支持 Windows 任务栏图标设置")};
#endif
}
quint32 WindowCapture::taskbarButtonCreatedMessage() {
#ifdef Q_OS_WIN
    static const auto message = RegisterWindowMessageW(L"TaskbarButtonCreated");
    return message;
#else
    return 0;
#endif
}
bool WindowCapture::isTaskbarButtonCreatedMessage(void *message, quintptr window) {
#ifdef Q_OS_WIN
    const auto event = static_cast<MSG *>(message);
    const auto expected = taskbarButtonCreatedMessage();
    return event && window && expected && event->message == expected &&
           event->hwnd == reinterpret_cast<HWND>(window);
#else
    Q_UNUSED(message);
    Q_UNUSED(window);
    return false;
#endif
}
QString WindowCapture::supportError() {
#ifdef Q_OS_WIN
    using VersionFunction = LONG(WINAPI *)(OSVERSIONINFOW *);
    const auto function =
        reinterpret_cast<VersionFunction>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    if (!function || function(&version) != 0)
        return QStringLiteral("无法确认 Windows 版本，未启用捕获排除");
    if (!supportedVersion(int(version.dwMajorVersion), int(version.dwBuildNumber)))
        return QStringLiteral("完整捕获排除需要 Windows 10 2004 或更高版本");
    BOOL composition = FALSE;
    if (FAILED(DwmIsCompositionEnabled(&composition)) || !composition)
        return QStringLiteral("桌面合成不可用，无法启用捕获排除");
    return {};
#else
    return QStringLiteral("当前系统不支持 Windows 捕获排除");
#endif
}
WindowCaptureResult WindowCapture::setExcluded(quintptr window, bool excluded) {
#ifdef Q_OS_WIN
    const auto handle = reinterpret_cast<HWND>(window);
    if (!handle || !IsWindow(handle))
        return {false, QStringLiteral("窗口句柄无效，未设置捕获排除")};
    DWORD process = 0;
    GetWindowThreadProcessId(handle, &process);
    if (process != GetCurrentProcessId())
        return {false, QStringLiteral("只能设置本进程窗口的捕获排除")};
    if (GetAncestor(handle, GA_ROOT) != handle || (GetWindowLongPtrW(handle, GWL_STYLE) & WS_CHILD))
        return {false, QStringLiteral("捕获排除只支持顶层窗口")};
    if (excluded) {
        const auto unsupported = supportError();
        if (!unsupported.isEmpty())
            return {false, unsupported};
    }
    constexpr DWORD excludeFromCapture = 0x11;
    const DWORD expected = excluded ? excludeFromCapture : 0;
    if (!SetWindowDisplayAffinity(handle, expected))
        return {false, QStringLiteral("捕获排除设置失败（Windows 错误 %1）").arg(GetLastError())};
    DWORD actual = 0;
    const bool read = GetWindowDisplayAffinity(handle, &actual);
    if (!read || actual != expected) {
        const auto error = read ? ERROR_NOT_SUPPORTED : GetLastError();
        // Do not leave a downgraded WDA_MONITOR blackout when full exclusion
        // could not be confirmed. The caller still reports this as a failure.
        if (excluded)
            SetWindowDisplayAffinity(handle, 0);
        return {false, QStringLiteral("系统未确认捕获排除状态（Windows 错误 %1）").arg(error)};
    }
    return {true, {}};
#else
    Q_UNUSED(window);
    Q_UNUSED(excluded);
    return {false, supportError()};
#endif
}
