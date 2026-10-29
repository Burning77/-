#include "CaptureShortcuts.h"
#include <QSettings>
QKeySequence CaptureShortcuts::selection() {
    return QKeySequence::fromString(QSettings().value("shortcuts/selectionCopy", "Alt+C").toString(),
                                    QKeySequence::PortableText);
}
QKeySequence CaptureShortcuts::region() {
    return QKeySequence::fromString(QSettings().value("shortcuts/regionCapture", "Ctrl+Alt+A").toString(),
                                    QKeySequence::PortableText);
}
HotkeyResult CaptureShortcuts::validate(QKeySequence copy, QKeySequence shot, QKeySequence boss) {
    for (const auto &key : {copy, shot}) {
        const auto valid = GlobalHotkey::validate(key);
        if (!valid.ok)
            return valid;
    }
    if (copy == shot || copy == boss || shot == boss)
        return {false, QStringLiteral("取词、区域截图和老板键必须使用不同的组合键")};
    return {true, {}};
}
