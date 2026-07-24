// src/core/notifications.cpp — Desktop notifications via QSystemTrayIcon.
//
// On PineTab 2 (Phosh) and most Linux desktops, QSystemTrayIcon uses
// StatusNotifierItem D-Bus protocol to display notifications.
#include "notifications.hpp"
#include "shared/theme.hpp"

#include <QIcon>
#include <QApplication>
#include <QPixmap>
#include <QPainter>
#include <QFont>

namespace progressive::desktop {

namespace {
inline constexpr int kTrayIconW = 32;
inline constexpr int kTrayIconH = 32;
inline constexpr int kNotifyMs  = 5000;
inline constexpr int kTrayFontPx = 24;
} // namespace

DesktopNotifier::DesktopNotifier(QObject* parent) : QObject(parent) {}

DesktopNotifier::~DesktopNotifier() {
    if (tray_) { tray_->hide(); delete tray_; tray_ = nullptr; }
}

bool DesktopNotifier::init() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        return false;
    }
    if (tray_) return true;
    tray_ = new QSystemTrayIcon(this);
    QPixmap pix(kTrayIconW, kTrayIconH);
    pix.fill(Design::trayIconBg);
    QPainter pp(&pix);
    pp.setRenderHint(QPainter::Antialiasing);
    QFont iconFont; iconFont.setBold(true); iconFont.setPixelSize(kTrayFontPx);
    pp.setFont(iconFont);
    pp.setPen(Design::trayIconText);
    pp.drawText(pix.rect(), Qt::AlignCenter, "P");
    pp.end();
    tray_->setIcon(QIcon(pix));
    tray_->setToolTip("Progressive Chat");
    tray_->show();
    return true;
}

void DesktopNotifier::notify(const QString& title, const QString& body) {
    if (!enabled_ || !tray_) return;
    tray_->showMessage(title, body, QSystemTrayIcon::Information, kNotifyMs);
}

} // namespace progressive::desktop
