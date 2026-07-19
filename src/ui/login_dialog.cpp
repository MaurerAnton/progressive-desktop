// src/ui/login_dialog.cpp

#include "login_dialog.hpp"

#include <QApplication>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QStandardPaths>
#include <QDir>
#include <QTimer>
#include <QString>

#include <progressive/well_known.hpp>

namespace progressive::desktop {

LoginDialog::LoginDialog(MatrixClient* client, SessionStore* store, QWidget* parent)
    : QDialog(parent), client_(client), store_(store) {

    setWindowTitle("Progressive Chat — Login");
    setModal(true);

    serverEdit_ = new QLineEdit("matrix.org", this);
    userEdit_ = new QLineEdit(this);
    userEdit_->setPlaceholderText("@user:server or username");
    passEdit_ = new QLineEdit(this);
    passEdit_->setEchoMode(QLineEdit::Password);
    passEdit_->setPlaceholderText("password");
    statusLabel_ = new QLabel("Enter your Matrix credentials.", this);
    statusLabel_->setWordWrap(true);

    auto* form = new QFormLayout;
    form->addRow("Server:", serverEdit_);
    form->addRow("User:", userEdit_);
    form->addRow("Password:", passEdit_);

    auto* loginBtn = new QPushButton("Login", this);
    loginBtn->setDefault(true);
    auto* cancelBtn = new QPushButton("Cancel", this);

    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    btnRow->addWidget(loginBtn);
    btnRow->addWidget(cancelBtn);

    auto* root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addWidget(statusLabel_);
    root->addLayout(btnRow);

    connect(loginBtn, &QPushButton::clicked, this, &LoginDialog::onLoginClicked);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
}

void LoginDialog::onLoginClicked() {
    auto server = serverEdit_->text().trimmed();
    auto user = userEdit_->text().trimmed();
    auto pass = passEdit_->text();

    if (server.isEmpty() || user.isEmpty() || pass.isEmpty()) {
        statusLabel_->setText("Fill in all fields.");
        return;
    }

    statusLabel_->setText("Discovering homeserver...");
    QApplication::processEvents();

    // Strip the leading @ and :server part if user entered full mxid
    std::string userStr = user.toStdString();
    if (userStr.size() > 0 && userStr[0] == '@') {
        // @user:server  →  username (server comes from serverEdit)
        auto colon = userStr.find(':');
        if (colon != std::string::npos) userStr = userStr.substr(1, colon - 1);
        else userStr = userStr.substr(1);
    }

    auto discovered = client_->discoverHomeserver(server.toStdString());
    if (!discovered.ok) {
        statusLabel_->setText(QString("Discovery failed: %1")
            .arg(QString::fromStdString(discovered.error.message)));
        return;
    }

    // Set the discovered URL on the client so login goes to the right server.
    AccountInfo stubAcct;
    stubAcct.homeserverUrl = discovered.data;
    client_->setAccount(stubAcct);

    statusLabel_->setText(QString("Logging into %1...").arg(
        QString::fromStdString(discovered.data)));
    QApplication::processEvents();

    auto result = client_->loginWithPassword(userStr, pass.toStdString());
    if (!result.ok) {
        statusLabel_->setText(QString("Login failed (%1): %2")
            .arg(QString::fromStdString(result.error.code))
            .arg(QString::fromStdString(result.error.message)));
        return;
    }

    // Persist session
    if (store_) {
        QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(dataDir);
        QString dbPath = dataDir + "/session.db";
        store_->open(dbPath.toStdString());
    }
    client_->setSessionStore(store_);
    client_->persistSession();

    logged_in_ = true;
    accept();
}

} // namespace progressive::desktop
