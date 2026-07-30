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
#include <QSettings>
#include <QCoreApplication>

namespace progressive::desktop {

void applyDarkTheme(QApplication& app) {
    app.setStyle(QStyleFactory::create("Fusion"));

    QPalette p;

    p.setColor(QPalette::Window,          Design::viewBg);
    p.setColor(QPalette::WindowText,      Design::reactionTextColor);
    p.setColor(QPalette::Base,            Design::inputBg);
    p.setColor(QPalette::AlternateBase,   QColor(45, 45, 45));
    p.setColor(QPalette::ToolTipBase,     QColor(0, 0, 0));
    p.setColor(QPalette::ToolTipText,     Design::reactionTextColor);
    p.setColor(QPalette::PlaceholderText, Design::dimTextColor);
    p.setColor(QPalette::Text,            Design::reactionTextColor);
    p.setColor(QPalette::Button,          QColor(45, 45, 45));
    p.setColor(QPalette::ButtonText,      Design::reactionTextColor);
    p.setColor(QPalette::BrightText,      Qt::red);
    p.setColor(QPalette::Link,            Design::accentColor);
    p.setColor(QPalette::LinkVisited,     QColor(150, 50, 200));

    p.setColor(QPalette::Highlight,       Design::selectedBg);
    p.setColor(QPalette::HighlightedText, Design::reactionTextColor);

    p.setColor(QPalette::Disabled, QPalette::WindowText, Design::dimTextColor);
    p.setColor(QPalette::Disabled, QPalette::Text,       Design::dimTextColor);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, Design::dimTextColor);

    app.setPalette(p);

    QString ss;
    ss += "QListView { border: none; background: " + Design::viewBg.name() + "; }";
    ss += "QListView::item { padding: 6px 8px; }";
    ss += "QListView::item:selected { background: #325082; color: #ffffff; }";
    ss += "QTextBrowser { background: " + Design::inputBg.name() + "; color: " + Design::reactionTextColor.name() + "; border: none; font-weight:400; }";
    ss += "QLineEdit, QTextEdit { background: " + Design::inputBg.name() + "; color: " + Design::reactionTextColor.name() + "; border: 1px solid " + Design::borderColor.name() + "; }";
    ss += "QLineEdit:focus, QTextEdit:focus { border: 1px solid " + Design::accentColor.name() + "; }";
    ss += "QPushButton { background: #2d2d2d; color: " + Design::reactionTextColor.name() + "; border: 1px solid " + Design::borderColor.name() + "; padding: 6px 16px; }";
    ss += "QPushButton:hover { background: #383838; border: 1px solid " + Design::hoverBorder.name() + "; }";
    ss += "QPushButton:pressed { background: " + Design::viewBg.name() + "; }";
    ss += "QScrollBar:vertical { border: none; background: " + Design::viewBg.name() + "; width: 10px; }";
    ss += "QScrollBar::handle:vertical { background: #4a4a4a; border-radius: 5px; min-height: 20px; }";
    ss += "QScrollBar::handle:vertical:hover { background: #5a5a5a; }";
    ss += "QScrollBar::add-line, QScrollBar::sub-line { height: 0; }";
    ss += "QStatusBar { background: " + Design::viewBg.name() + "; color: " + Design::dimTextColor.name() + "; }";
    ss += "QStatusBar::item { border: none; }";
    ss += "QSplitter::handle { background: #2d2d2d; }";
    ss += "QLabel { color: " + Design::reactionTextColor.name() + "; }";
    ss += "QToolBar { background: " + Design::viewBg.name() + "; border: none; spacing: 4px; }";
    ss += "QToolBar QLabel { color: " + Design::reactionTextColor.name() + "; padding: 0 8px; }";
    app.setStyleSheet(ss);
}

void Theme::load() {
    QSettings s;
    Design::incomingBubble = QColor(s.value("theme/incomingBubble", Design::incomingBubble.name()).toString());
    Design::outgoingBubble = QColor(s.value("theme/outgoingBubble", Design::outgoingBubble.name()).toString());
    Design::accentColor   = QColor(s.value("theme/accentColor",   Design::accentColor.name()).toString());
    Design::viewBg         = QColor(s.value("theme/viewBg",        Design::viewBg.name()).toString());
    Design::textColor      = QColor(s.value("theme/textColor",     Design::textColor.name()).toString());
}

void Theme::save() {
    QSettings s;
    s.setValue("theme/incomingBubble", Design::incomingBubble.name());
    s.setValue("theme/outgoingBubble", Design::outgoingBubble.name());
    s.setValue("theme/accentColor",    Design::accentColor.name());
    s.setValue("theme/viewBg",         Design::viewBg.name());
    s.setValue("theme/textColor",      Design::textColor.name());
}

void Theme::reapply() {
    auto* app = qobject_cast<QApplication*>(QCoreApplication::instance());
    if (!app) return;
    applyDarkTheme(*app);
}

} // namespace progressive::desktop
