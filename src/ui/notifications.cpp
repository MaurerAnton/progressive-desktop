// src/core/notifications.cpp — Desktop notifications via QSystemTrayIcon.
//
// On PineTab 2 (Phosh) and most Linux desktops, QSystemTrayIcon uses
// StatusNotifierItem D-Bus protocol to display notifications.
#include "notifications.hpp"

#include <QIcon>
#include <QApplication>
#include <QPixmap>

namespace progressive::desktop {

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
    QPixmap pix(32, 32);
    pix.fill(Qt::darkBlue);
    tray_->setIcon(QIcon(pix));
    tray_->setToolTip("Progressive Chat");
    tray_->show();
    return true;
}

void DesktopNotifier::notify(const QString& title, const QString& body) {
    if (!enabled_ || !tray_) return;
    tray_->showMessage(title, body, QSystemTrayIcon::Information, 5000);
}

} // namespace progressive::desktop
