#include "GlobalHotkey.h"
#include <QCoreApplication>
#include <QGuiApplication>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
struct NativeKey {
    quint32 modifiers = 0, key = 0;
};
NativeKey translate(const QKeySequence &sequence) {
    if (sequence.count() != 1)
        return {};
    const auto combination = sequence[0];
    const auto modifiers = combination.keyboardModifiers();
    if (modifiers & ~(Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier))
        return {};
    NativeKey result;
    if (modifiers & Qt::AltModifier)
        result.modifiers |= 1;
    if (modifiers & Qt::ControlModifier)
        result.modifiers |= 2;
    if (modifiers & Qt::ShiftModifier)
        result.modifiers |= 4;
    const auto key = combination.key();
    const bool functionKey = key >= Qt::Key_F1 && key <= Qt::Key_F24 && key != Qt::Key_F12;
    if (!functionKey && !(result.modifiers & 3))
        return {};
    if ((key >= Qt::Key_A && key <= Qt::Key_Z) || (key >= Qt::Key_0 && key <= Qt::Key_9))
        result.key = key;
    else if (functionKey)
        result.key = 0x70 + key - Qt::Key_F1;
    else if (key == Qt::Key_Space)
        result.key = 0x20;
    // Avoid OS/task switching and the two existing in-window privacy shortcuts.
    if (result.modifiers == 6 && (key == Qt::Key_O || key == Qt::Key_P))
        return {};
    if (result.modifiers == 1 && key == Qt::Key_F4)
        return {};
    return result;
}
HotkeyResult nativeOperation(int id, quint32 modifiers, quint32 key, bool enable) {
#ifdef Q_OS_WIN
    if (QGuiApplication::platformName() != "windows")
        return {false, QStringLiteral("当前窗口平台不支持 Windows 全局快捷键")};
    const bool ok =
        enable ? RegisterHotKey(nullptr, id, modifiers | MOD_NOREPEAT, key) : UnregisterHotKey(nullptr, id);
    if (ok)
        return {true, {}};
    const auto error = GetLastError();
    return {false, error == ERROR_HOTKEY_ALREADY_REGISTERED
                       ? QStringLiteral("快捷键已被其他程序占用，请换一组按键")
                       : QStringLiteral("Windows 快捷键设置失败（错误 %1），请换一组按键").arg(error)};
#else
    Q_UNUSED(id);
    Q_UNUSED(modifiers);
    Q_UNUSED(key);
    Q_UNUSED(enable);
    return {false, QStringLiteral("全局老板键目前仅支持 Windows")};
#endif
}
} // namespace

GlobalHotkey::GlobalHotkey(QObject *parent, HotkeyBackend backend)
    : QObject(parent), invoke(backend ? std::move(backend) : nativeOperation) {
    // Instances live on the GUI thread. Two IDs allow changing the key without
    // releasing the user's working recovery shortcut before the new one succeeds.
    static int nextId = 0x5000;
    firstId = nextId;
    nextId += 2;
    if (qApp)
        qApp->installNativeEventFilter(this);
}
GlobalHotkey::~GlobalHotkey() {
    if (qApp)
        qApp->removeNativeEventFilter(this);
    if (activeId)
        invoke(activeId, currentModifiers, currentKey, false);
    for (auto it = pendingRemoval.cbegin(); it != pendingRemoval.cend(); ++it) {
        const auto removed = invoke(it.key(), it.value().first, it.value().second, false);
        if (!removed.ok)
            qWarning("Global hotkey cleanup failed: %s", qPrintable(removed.error));
    }
}
QKeySequence GlobalHotkey::defaultSequence() {
    return QKeySequence("Ctrl+Alt+Space");
}
HotkeyResult GlobalHotkey::validate(const QKeySequence &sequence) {
    if (!translate(sequence).key)
        return {false, QStringLiteral("请设置单组 Ctrl/Alt + 字母、数字或空格，或 F1–F11/F13–F24。"
                                      "F12、Alt+F4 和 Ctrl+Shift+O/P 不可用")};
    return {true, {}};
}
HotkeyResult GlobalHotkey::setSequence(const QKeySequence &sequence) {
    const auto valid = validate(sequence);
    if (!valid.ok)
        return valid;
    if (activeId && current == sequence)
        return {true, {}};
    for (auto it = pendingRemoval.begin(); it != pendingRemoval.end();) {
        const auto removed = invoke(it.key(), it.value().first, it.value().second, false);
        if (!removed.ok)
            return {false, QStringLiteral("上次改键的清理尚未成功：%1").arg(removed.error)};
        it = pendingRemoval.erase(it);
    }
    const auto native = translate(sequence);
    const int candidateId = activeId == firstId ? firstId + 1 : firstId;
    if (candidateId > 0xbfff)
        return {false, QStringLiteral("全局快捷键资源不足，请重启软件")};
    const auto installed = invoke(candidateId, native.modifiers, native.key, true);
    if (!installed.ok)
        return installed;
    if (activeId) {
        const auto removed = invoke(activeId, currentModifiers, currentKey, false);
        if (!removed.ok) {
            const auto rollback = invoke(candidateId, native.modifiers, native.key, false);
            if (!rollback.ok) {
                pendingRemoval.insert(candidateId, {native.modifiers, native.key});
                return {false,
                        QStringLiteral("%1；新按键回退清理失败：%2").arg(removed.error, rollback.error)};
            }
            return removed;
        }
    }
    activeId = candidateId;
    currentModifiers = native.modifiers;
    currentKey = native.key;
    current = sequence;
    emit changed();
    return {true, {}};
}
bool GlobalHotkey::nativeEventFilter(const QByteArray &type, void *message, qintptr *result) {
#ifdef Q_OS_WIN
    if ((type == "windows_dispatcher_MSG" || type == "windows_generic_MSG") && message && activeId) {
        const auto event = static_cast<MSG *>(message);
        if (event->message == WM_HOTKEY && !event->hwnd && int(event->wParam) == activeId &&
            LOWORD(event->lParam) == currentModifiers && HIWORD(event->lParam) == currentKey) {
            if (result)
                *result = 0;
            emit activated();
            return true;
        }
    }
#else
    Q_UNUSED(type);
    Q_UNUSED(message);
    Q_UNUSED(result);
#endif
    return false;
}
