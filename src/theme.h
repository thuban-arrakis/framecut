#pragma once
#include <QColor>

namespace Theme {
bool isLight();
QColor color(const char *darkHex, const char *lightHex);
void applyPalette();
}
