// src/ui/login_dialog.cpp

#include "login_dialog.hpp"

#include <QApplication>
#include <QDesktopServices>
#include <QUrl>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QStandardPaths>
#include <QDir>
#include <QTimer>
#include <QString>
#include <QCheckBox>

#include <progressive/well_known.hpp>

namespace progressive::desktop {

LoginDialog::LoginDialog(MatrixClient* client, SessionStore* store, QWidget* parent)
    : QDialog(parent), client_(client), store_(store) {

    setWindowTitle("Progressive Chat — Login");
    setModal(true);

    serverEdit_ = new QLineEdit("matrix.org", this);
    userEdit_ = new QLineEdit(this);
    userEdit_->setPlaceholderText("username (NOT @user:server — just the name)");
    passEdit_ = new QLineEdit(this);
    passEdit_->setEchoMode(QLineEdit::Password);
    passEdit_->setPlaceholderText("password");
    showPassCheck_ = new QCheckBox("Show password", this);

    statusLabel_ = new QLabel("Enter your Matrix credentials.\n"
                              "No account? Click Register to create one on the homeserver.", this);
    statusLabel_->setWordWrap(true);

    auto* form = new QFormLayout;
    form->addRow("Server:", serverEdit_);
    form->addRow("User:", userEdit_);
    form->addRow("Password:", passEdit_);
    form->addRow("", showPassCheck_);

    auto* loginBtn = new QPushButton("Login", this);
    loginBtn->setDefault(true);
    auto* registerBtn = new QPushButton("Register", this);
    auto* cancelBtn = new QPushButton("Cancel", this);

    auto* btnRow = new QHBoxLayout;
    btnRow->addWidget(registerBtn);
    btnRow->addStretch();
    btnRow->addWidget(loginBtn);
    btnRow->addWidget(cancelBtn);

    auto* root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addWidget(statusLabel_);
    root->addLayout(btnRow);

    connect(loginBtn, &QPushButton::clicked, this, &LoginDialog::onLoginClicked);
    connect(registerBtn, &QPushButton::clicked, this, &LoginDialog::onRegisterClicked);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(showPassCheck_, &QCheckBox::toggled, this,
            [this](bool checked) { onShowPasswordToggled(checked); });
}

void LoginDialog::onShowPasswordToggled(bool checked) {
    passEdit_->setEchoMode(checked ? QLineEdit::Normal : QLineEdit::Password);
}

void LoginDialog::onRegisterClicked() {
    auto server = serverEdit_->text().trimmed();
    auto user = userEdit_->text().trimmed();
    auto pass = passEdit_->text();

    if (server.isEmpty()) {
        statusLabel_->setText("Enter a server name first.");
        return;
    }
    if (user.isEmpty() || pass.isEmpty()) {
        statusLabel_->setText("Enter username and password to register.\n"
                              "Username is just the name (e.g. 'alice').");
        return;
    }

    // Strip @ and :server from username if user entered the full Matrix ID
    std::string userStr = user.toStdString();
    if (userStr.size() > 0 && userStr[0] == '@') {
        auto colon = userStr.find(':');
        if (colon != std::string::npos) userStr = userStr.substr(1, colon - 1);
        else userStr = userStr.substr(1);
    }

    statusLabel_->setText("Discovering homeserver...");
    QApplication::processEvents();

    // Discover the homeserver
    auto discovered = client_->discoverHomeserver(server.toStdString());
    if (!discovered.ok) {
        statusLabel_->setText(QString("Discovery failed: %1").arg(
            QString::fromStdString(discovered.error.message)));
        return;
    }

    statusLabel_->setText(QString("Registering on %1...").arg(
        QString::fromStdString(discovered.data)));
    QApplication::processEvents();

    // Try in-app registration
    auto result = client_->registerAccount(userStr, pass.toStdString(), discovered.data);
    if (result.ok) {
        // Registration succeeded — we're logged in!
        client_->setAccount(result.data);
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
        return;
    }

    // If captcha is needed, fall back to browser
    if (result.error.code == "M_NEEDS_CAPTCHA") {
        QString regUrl;
        if (server == "matrix.org" || server.contains("matrix.org")) {
            regUrl = "https://app.element.io/#/register";
        } else {
            regUrl = "https://" + server + "/#/register";
        }
        QDesktopServices::openUrl(QUrl(regUrl));
        statusLabel_->setText(QString("This server requires captcha for registration.\n"
                                      "Opened registration page in your browser:\n%1\n"
                                      "After registering, come back here and login.")
            .arg(regUrl));
        return;
    }

    // Other error
    statusLabel_->setText(QString("Registration failed (%1): %2")
        .arg(QString::fromStdString(result.error.code))
        .arg(QString::fromStdString(result.error.message)));
}

void LoginDialog::onLoginClicked() {
    auto server = serverEdit_->text().trimmed();
    auto user = userEdit_->text().trimmed();
    auto pass = passEdit_->text();

    if (server.isEmpty() || user.isEmpty() || pass.isEmpty()) {
        statusLabel_->setText("Fill in all fields.\n"
                              "Username is just the name (e.g. 'alice'), "
                              "NOT the full Matrix ID.");
        return;
    }

    statusLabel_->setText("Discovering homeserver...");
    QApplication::processEvents();

    // Strip @ and :server from username if user entered the full Matrix ID.
    // matrix.org's /login with m.id.user expects just 'alice', not '@alice:matrix.org'.
    std::string userStr = user.toStdString();
    if (userStr.size() > 0 && userStr[0] == '@') {
        auto colon = userStr.find(':');
        if (colon != std::string::npos) userStr = userStr.substr(1, colon - 1);
        else userStr = userStr.substr(1);
    }

    auto discovered = client_->discoverHomeserver(server.toStdString());
    if (!discovered.ok) {
        statusLabel_->setText(QString("Discovery failed: %1\n"
                                      "Check the server name (e.g. 'matrix.org').")
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
        QString hint;
        if (result.error.code == "M_FORBIDDEN") {
            hint = "\n\nM_FORBIDDEN means wrong username or password.\n"
                   "Check:\n"
                   "  - Username is just 'alice' (no @, no :server)\n"
                   "  - Password is correct (use Show password to verify)\n"
                   "  - The account exists on this homeserver";
        }
        statusLabel_->setText(QString("Login failed (%1): %2%3")
            .arg(QString::fromStdString(result.error.code))
            .arg(QString::fromStdString(result.error.message))
            .arg(hint));
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
