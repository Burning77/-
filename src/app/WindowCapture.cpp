#include "WindowCapture.h"
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#endif

bool WindowCapture::supportedVersion(int major, int build) {
    return major > 10 || (major == 10 && build >= 19041);
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
