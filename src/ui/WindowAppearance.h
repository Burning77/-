#pragma once
#include <QColor>
#include <QPalette>
#include <QString>
namespace WindowAppearance {
QColor surface(bool transparent, int opacity);
QPalette palette(bool transparent);
QString styleSheet(bool transparent);
} // namespace WindowAppearance
