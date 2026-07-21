// src/ui/login_dialog.hpp — modal login dialog.
//
// Calls MatrixClient::discoverHomeserver + getVersions + loginWithPassword.
// On success, persists session via SessionStore and accepts the dialog.

#pragma once

#include "core/matrix_client.hpp"
#include "core/session_store.hpp"

#include <QDialog>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>

namespace progressive::desktop {

class LoginDialog : public QDialog {
    Q_OBJECT

public:
    LoginDialog(MatrixClient* client, SessionStore* store, QWidget* parent = nullptr);

    // After exec() returns QDialog::Accepted, the MatrixClient has account_ set.
    bool loggedIn() const { return logged_in_; }

private slots:
    void onLoginClicked();
    void onRegisterClicked();
    void onShowPasswordToggled(bool checked);

private:
    MatrixClient* client_;
    SessionStore* store_;
    bool logged_in_ = false;

    QLineEdit* serverEdit_;
    QLineEdit* userEdit_;
    QLineEdit* passEdit_;
    QLabel* statusLabel_;
    QCheckBox* showPassCheck_;
};

} // namespace progressive::desktop
