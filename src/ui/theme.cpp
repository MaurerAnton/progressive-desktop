// src/ui/theme.cpp — dark palette, Fusion style.
//
// The color palette is the canonical "Qt dark theme" recipe — same one
// used by KDE's Breeze Dark, Qt Creator's dark mode, and many Matrix clients
// (Nheko, neo). Tuned for readability on PineTab 2's 10.1" IPS screen (600
// nits, fixed backlight) — high contrast, no pure white text (causes
// halation), no pure black backgrounds (causes smearing).

#include "theme.hpp"

#include <QApplication>
#include <QPalette>
#include <QColor>
#include <QStyleFactory>

namespace progressive::desktop {

void applyDarkTheme(QApplication& app) {
    // Fusion style — looks identical on X11, Wayland, PineTab 2, macOS.
    // Without this, Qt uses the platform default (e.g. GtkStyle on Linux),
    // which may not respect QPalette for widgets.
    app.setStyle(QStyleFactory::create("Fusion"));

    QPalette p;

    // Window background — slightly off-black to avoid smearing
    const QColor bg(30, 30, 30);          // #1e1e1e
    const QColor altBg(45, 45, 45);       // #2d2d2d — for alternating rows
    const QColor base(20, 20, 20);       // #141414 — input/list background
    const QColor text(232, 232, 232);    // #e8e8e8 — not pure white (no halation)
    const QColor textDim(150, 150, 150); // #969696 — timestamps, hints
    const QColor accent(42, 130, 218);   // #2a82da — Matrix blue, links/highlights
    const QColor highlight(50, 80, 130); // darker selection on dark

    p.setColor(QPalette::Window,          bg);
    p.setColor(QPalette::WindowText,      text);
    p.setColor(QPalette::Base,            base);
    p.setColor(QPalette::AlternateBase,   altBg);
    p.setColor(QPalette::ToolTipBase,     QColor(0, 0, 0));
    p.setColor(QPalette::ToolTipText,     text);
    p.setColor(QPalette::PlaceholderText, textDim);
    p.setColor(QPalette::Text,            text);
    p.setColor(QPalette::Button,          altBg);
    p.setColor(QPalette::ButtonText,      text);
    p.setColor(QPalette::BrightText,      Qt::red);
    p.setColor(QPalette::Link,            accent);
    p.setColor(QPalette::LinkVisited,     QColor(150, 50, 200));

    // Selection (e.g. clicked room in the list)
    p.setColor(QPalette::Highlight,       highlight);
    p.setColor(QPalette::HighlightedText, text);

    // Disabled state — slightly dimmer
    p.setColor(QPalette::Disabled, QPalette::WindowText, textDim);
    p.setColor(QPalette::Disabled, QPalette::Text,       textDim);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, textDim);

    app.setPalette(p);

    // Also apply a small stylesheet for widgets that don't respect QPalette
    // by default (e.g. QTextBrowser's link color, QListView's border).
    // font-weight:400 (normal) on body text prevents the "thin/stroke" look
    // some users see on dark backgrounds with subpixel rendering.
    app.setStyleSheet(
        "QListView { border: none; background: #1e1e1e; }"
        "QListView::item { padding: 6px 8px; }"
        "QListView::item:selected { background: #325082; color: #ffffff; }"
        "QTextBrowser { background: #141414; color: #e8e8e8; border: none; font-weight:400; }"
        "QLineEdit, QTextEdit { background: #141414; color: #e8e8e8; border: 1px solid #3a3a3a; }"
        "QLineEdit:focus, QTextEdit:focus { border: 1px solid #2a82da; }"
        "QPushButton { background: #2d2d2d; color: #e8e8e8; border: 1px solid #3a3a3a; padding: 6px 16px; }"
        "QPushButton:hover { background: #383838; border: 1px solid #4a4a4a; }"
        "QPushButton:pressed { background: #1e1e1e; }"
        "QScrollBar:vertical { border: none; background: #1e1e1e; width: 10px; }"
        "QScrollBar::handle:vertical { background: #4a4a4a; border-radius: 5px; min-height: 20px; }"
        "QScrollBar::handle:vertical:hover { background: #5a5a5a; }"
        "QScrollBar::add-line, QScrollBar::sub-line { height: 0; }"
        "QStatusBar { background: #1e1e1e; color: #969696; }"
        "QStatusBar::item { border: none; }"
        "QSplitter::handle { background: #2d2d2d; }"
        "QLabel { color: #e8e8e8; }"
        "QToolBar { background: #1e1e1e; border: none; spacing: 4px; }"
        "QToolBar QLabel { color: #e8e8e8; padding: 0 8px; }"
    );
}

} // namespace progressive::desktop
