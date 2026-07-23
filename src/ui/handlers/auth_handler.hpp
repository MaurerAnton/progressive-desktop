// src/ui/auth_handler.hpp — login/logout/forceReLogin extracted from MainWindow.
#pragma once
#include <memory>
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
    AuthHandler(std::shared_ptr<MatrixClient> client, std::shared_ptr<SessionStore> store, SyncEngine* sync,
                QLabel* userLabel, QLabel* statusLabel, QObject* parent = nullptr);

    void setClient(std::shared_ptr<MatrixClient> c) { client_ = std::move(c); }

    void showLoginDialog();
    void forceReLogin();
    void logout();

signals:
    void loggedIn();
    void loggedOut();

private slots:
    void onLoginDialogAccepted();

private:
    std::shared_ptr<MatrixClient> client_;
    std::shared_ptr<SessionStore> store_;
    SyncEngine* sync_;
    QLabel* userLabel_;
    QLabel* statusLabel_;
};

} // namespace progressive::desktop
