// src/ui/auth_handler.cpp — login/logout/forceReLogin extracted from MainWindow.
#include "auth_handler.hpp"
#include "dialogs/login_dialog.hpp"
#include "core/matrix_client.hpp"
#include "core/session_store.hpp"
#include "core/sync_engine.hpp"

#include <QMessageBox>
#include <QLabel>

namespace progressive::desktop {

AuthHandler::AuthHandler(MatrixClient* client, SessionStore* store, SyncEngine* sync,
                           QLabel* userLabel, QLabel* statusLabel, QObject* parent)
    : QObject(parent), client_(client), store_(store), sync_(sync),
      userLabel_(userLabel), statusLabel_(statusLabel) {}

void AuthHandler::showLoginDialog() {
    LoginDialog dlg(client_, store_, qobject_cast<QWidget*>(parent()));
    connect(&dlg, &QDialog::accepted, this, &AuthHandler::onLoginDialogAccepted);
    dlg.exec();
}

void AuthHandler::onLoginDialogAccepted() {
    if (!client_ || !client_->isLoggedIn()) return;
    userLabel_->setText(" " + QString::fromStdString(client_->account().userId) + " ");
    statusLabel_->setText("Starting sync...");
    sync_->setClient(client_);
    sync_->setSessionStore(store_);
    sync_->start();
    emit loggedIn();
}

void AuthHandler::forceReLogin() {
    sync_->stop();
    QMessageBox::warning(qobject_cast<QWidget*>(parent()), "Session Expired",
        "Your session has expired. This can happen if:\n"
        "  - The server was restarted\n"
        "  - You changed your password\n"
        "  - You logged out from another device\n"
        "  - The app was force-closed during a sync\n\n"
        "Your chats and settings are preserved. Please login again.");
    userLabel_->setText(" [Session expired] ");
    statusLabel_->setStyleSheet("color: red; font-weight: bold;");
    statusLabel_->setText("Session expired — login required");
    showLoginDialog();
    statusLabel_->setStyleSheet("");
}

void AuthHandler::logout() {
    sync_->stop();
    if (client_) client_->logout();
    if (store_) store_->clearAccount();
    userLabel_->setText(" Not logged in ");
    statusLabel_->setText("Logged out.");
    emit loggedOut();
}

} // namespace progressive::desktop
