// src/core/notifications.hpp — Desktop notifications for highlight events.
#pragma once
#include <QObject>
#include <QString>
#include <QSystemTrayIcon>
#include <memory>

namespace progressive::desktop {

class DesktopNotifier : public QObject {
    Q_OBJECT
public:
    explicit DesktopNotifier(QObject* parent = nullptr);
    ~DesktopNotifier();

    // Initialize the system tray icon. Call once at startup if notifications
    // are desired. Returns true if the tray was successfully created.
    bool init();

    // Show a desktop notification.
    // title: typically the room name
    // body: the message body or a summary
    // If the tray icon isn't available, this is a no-op (silent fallback).
    void notify(const QString& title, const QString& body);

    // Set whether to show notifications at all (user preference).
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }

private:
    bool enabled_ = true;
    QSystemTrayIcon* tray_ = nullptr;
};

} // namespace progressive::desktop
