#pragma once
#include "app/GlobalHotkey.h"
namespace CaptureShortcuts {
QKeySequence selection();
QKeySequence region();
HotkeyResult validate(QKeySequence selection, QKeySequence region, QKeySequence boss);
} // namespace CaptureShortcuts
