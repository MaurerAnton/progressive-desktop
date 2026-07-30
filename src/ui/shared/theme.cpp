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
    for (const auto& e : tokenRegistry()) {
        *e.field = QColor(s.value(e.qkey, e.field->name()).toString());
    }
}

void Theme::save() {
    QSettings s;
    for (const auto& e : tokenRegistry()) {
        s.setValue(e.qkey, e.field->name());
    }
}

const std::vector<Theme::TokenEntry>& Theme::tokenRegistry() {
    static const std::vector<TokenEntry> reg = {
        {"theme/viewBg",            &Design::viewBg},
        {"theme/inputBg",           &Design::inputBg},
        {"theme/selectedBg",        &Design::selectedBg},
        {"theme/inviteRowBg",       &Design::inviteRowBg},
        {"theme/incomingBubble",    &Design::incomingBubble},
        {"theme/outgoingBubble",    &Design::outgoingBubble},
        {"theme/textColor",         &Design::textColor},
        {"theme/timeColor",         &Design::timeColor},
        {"theme/systemTextColor",   &Design::systemTextColor},
        {"theme/mutedTextColor",    &Design::mutedTextColor},
        {"theme/dimTextColor",      &Design::dimTextColor},
        {"theme/reactionTextColor", &Design::reactionTextColor},
        {"theme/inviteTextColor",   &Design::inviteTextColor},
        {"theme/deletedTextColor",  &Design::deletedTextColor},
        {"theme/accentColor",       &Design::accentColor},
        {"theme/pinnedColor",       &Design::pinnedColor},
        {"theme/threadColor",       &Design::threadColor},
        {"theme/typingColor",       &Design::typingColor},
        {"theme/emoteColor",        &Design::emoteColor},
        {"theme/linkOnOutgoing",    &Design::linkOnOutgoing},
        {"theme/unreadBadgeColor",  &Design::unreadBadgeColor},
        {"theme/playBtnOverlay",    &Design::playBtnOverlay},
        {"theme/dangerText",        &Design::dangerText},
        {"theme/dangerBg",          &Design::dangerBg},
        {"theme/acceptBg",          &Design::acceptBg},
        {"theme/reactionBg",        &Design::reactionBg},
        {"theme/borderColor",       &Design::borderColor},
        {"theme/hoverBorder",       &Design::hoverBorder},
        {"theme/replyLineColor",    &Design::replyLineColor},
        {"theme/fileCardBg",        &Design::fileCardBg},
        {"theme/fileCardBorder",    &Design::fileCardBorder},
        {"theme/fileCardIconText",  &Design::fileCardIconText},
        {"theme/fileCardFileName",  &Design::fileCardFileName},
        {"theme/fileAudioBar",      &Design::fileAudioBar},
        {"theme/fileFileBar",       &Design::fileFileBar},
        {"theme/imgPlaceholderBg",  &Design::imgPlaceholderBg},
        {"theme/trayIconBg",        &Design::trayIconBg},
        {"theme/logViewBg",         &Design::logViewBg},
        {"theme/logViewText",       &Design::logViewText},
        {"theme/httpGetColor",      &Design::httpGetColor},
        {"theme/httpPostColor",     &Design::httpPostColor},
        {"theme/httpPutColor",      &Design::httpPutColor},
        {"theme/httpErrorColor",    &Design::httpErrorColor},
        {"theme/http2xxColor",      &Design::http2xxColor},
        {"theme/httpOtherStatusColor", &Design::httpOtherStatusColor},
        {"theme/httpOtherMethodColor", &Design::httpOtherMethodColor},
        {"theme/accountComboBg",    &Design::accountComboBg},
        {"theme/accountComboText",  &Design::accountComboText},
    };
    return reg;
}

void Theme::reapply() {
    auto* app = qobject_cast<QApplication*>(QCoreApplication::instance());
    if (!app) return;
    applyDarkTheme(*app);
    for (auto& cb : listeners()) cb();
}

std::vector<std::function<void()>>& Theme::listeners() {
    static std::vector<std::function<void()>> v;
    return v;
}

void Theme::addListener(std::function<void()> cb) {
    listeners().push_back(std::move(cb));
}

} // namespace progressive::desktop
