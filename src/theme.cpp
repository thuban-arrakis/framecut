#include "theme.h"
#include <QApplication>
#include <QPalette>
#include <QStyle>
#include <QVariant>

namespace Theme {
bool isLight() { return qApp && qApp->property("framecutTheme").toString() == "light"; }
QColor color(const char *darkHex, const char *lightHex) {
    return QColor(QString::fromLatin1(isLight() ? lightHex : darkHex));
}
void applyPalette() {
    if (!qApp) return;
    QPalette palette = qApp->style()->standardPalette();
    palette.setColor(QPalette::Window, color("#202522", "#f5f7f6"));
    palette.setColor(QPalette::WindowText, color("#d6e2d9", "#23342b"));
    palette.setColor(QPalette::Base, color("#171b19", "#ffffff"));
    palette.setColor(QPalette::AlternateBase, color("#28342c", "#edf2ee"));
    palette.setColor(QPalette::Text, color("#d6e2d9", "#23342b"));
    palette.setColor(QPalette::Button, color("#303d34", "#e6ede8"));
    palette.setColor(QPalette::ButtonText, color("#d6e2d9", "#263c2f"));
    palette.setColor(QPalette::Highlight, color("#81e9bc", "#167c57"));
    palette.setColor(QPalette::HighlightedText, color("#143c28", "#ffffff"));
    palette.setColor(QPalette::ToolTipBase, color("#303d34", "#ffffff"));
    palette.setColor(QPalette::ToolTipText, color("#e4ece6", "#23342b"));
    palette.setColor(QPalette::Link, color("#81e9bc", "#146a4c"));
    palette.setColor(QPalette::LinkVisited, color("#afd9c5", "#506b5a"));
    palette.setColor(QPalette::PlaceholderText, color("#829388", "#62766a"));
    for (auto role : {QPalette::Text, QPalette::ButtonText, QPalette::WindowText})
        palette.setColor(QPalette::Disabled, role, color("#627769", "#88978f"));
    qApp->setPalette(palette);
}
}
