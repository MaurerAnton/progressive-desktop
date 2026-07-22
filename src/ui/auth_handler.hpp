// src/ui/auth_handler.hpp — login/logout/forceReLogin extracted from MainWindow.
#pragma once
#include <QObject>
#include <QString>
#include <string>

class QLabel;

namespace progressive::desktop {

class MatrixClient;
class SessionStore;
class SyncEngine;
class LoginDialog;

class AuthHandler : public QObject {
    Q_OBJECT
public:
    AuthHandler(MatrixClient* client, SessionStore* store, SyncEngine* sync,
                QLabel* userLabel, QLabel* statusLabel, QObject* parent = nullptr);

    void showLoginDialog();
    void forceReLogin();
    void logout();

signals:
    void loggedIn();
    void loggedOut();

private slots:
    void onLoginDialogAccepted();

private:
    MatrixClient* client_;
    SessionStore* store_;
    SyncEngine* sync_;
    QLabel* userLabel_;
    QLabel* statusLabel_;
};

} // namespace progressive::desktop
