// src/ui/login_dialog.cpp

#include "login_dialog.hpp"

#include <QApplication>
#include <QDesktopServices>
#include <QUrl>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QTimer>
#include <QString>
#include <QCheckBox>
#include <QSettings>

#include "core/utils.hpp"
#include "core/debug_log.hpp"

#include <filesystem>
#include <cstdlib>

#include <progressive/well_known.hpp>

namespace progressive::desktop {

LoginDialog::LoginDialog(MatrixClient* client, SessionStore* store, QWidget* parent)
    : QDialog(parent), client_(client), store_(store) {

    setWindowTitle("Progressive Chat — Login");
    setModal(true);

    serverCombo_ = new QComboBox(this);
    serverCombo_->setEditable(true);
    serverCombo_->setInsertPolicy(QComboBox::NoInsert);
    serverCombo_->addItem("matrix.org");
    serverCombo_->addItem("envs.net");
    serverCombo_->addItem("tchncs.de");
    serverCombo_->addItem("chat.kde.org");
    serverCombo_->addItem("mozilla.org");
    QSettings s;
    QString lastServer = s.value("login/lastServer", "matrix.org").toString();
    serverCombo_->setCurrentText(lastServer);
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
    form->addRow("Server:", serverCombo_);
    form->addRow("User:", userEdit_);
    form->addRow("Password:", passEdit_);
    form->addRow("", showPassCheck_);

    tokenEdit_ = new QLineEdit(this);
    tokenEdit_->setPlaceholderText("registration token (if required by server)");
    tokenEdit_->setToolTip("Registration token (for closed servers that require "
                           "one). Leave empty if your server doesn't require a "
                           "token. NOT used for login — registration tokens are "
                           "only for creating new accounts.");
    form->addRow("Reg token:", tokenEdit_);

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
    auto server = serverCombo_->currentText().trimmed();
    auto user = userEdit_->text().trimmed();
    auto pass = passEdit_->text();
    auto token = tokenEdit_->text().trimmed();

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

    QSettings s;
    s.setValue("login/lastServer", serverCombo_->currentText());

    // Try in-app registration
    auto result = client_->registerAccount(userStr, pass.toStdString(),
                                            discovered.data, token.toStdString());
    if (result.ok) {
        // Registration succeeded — we're logged in!
        client_->setAccount(result.data);
        if (store_) {
            const char* xdg = getenv("XDG_DATA_HOME");
            std::string dataPath;
            if (xdg && xdg[0]) { dataPath = std::string(xdg) + "/progressive-desktop"; }
            else { const char* home = getenv("HOME"); dataPath = std::string(home ? home : "/tmp") + "/.local/share/progressive-desktop"; }
            std::filesystem::create_directories(dataPath);
            QString dbPath = QString::fromStdString(dataPath + "/session.db");
        store_->open(dbPath.toStdString());
        LOG(LogChannel::E2EE, "login: store open ok=%d db=%p",
            store_->isOpen() ? 1 : 0, (void*)store_);
        }
        client_->setSessionStore(store_);
        client_->persistSession();
        logged_in_ = true;
        accept();
        return;
    }

    if (result.error.code == "M_REGISTRATION_TOKEN_INVALID") {
        statusLabel_->setText("Invalid or expired registration token.\n"
                              "Check with your server admin.");
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
    auto server = serverCombo_->currentText().trimmed();
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

    QSettings s;
    s.setValue("login/lastServer", serverCombo_->currentText());

    // Set the discovered URL on the client so login goes to the right server.
    auto current = client_->account();
    current.homeserverUrl = discovered.data;
    client_->setAccount(current);

    statusLabel_->setText(QString("Logging into %1...").arg(
        QString::fromStdString(discovered.data)));
    QApplication::processEvents();

    // Generate a unique device_id for this installation if not already set.
    // This prevents M_UNKNOWN_TOKEN when multiple devices share the same ID.
    std::string deviceId = client_->account().deviceId;
    if (deviceId.empty() || deviceId == "PROGRESSIVE_DESKTOP") {
        deviceId = generateUUID();
    }
    auto result = client_->loginWithPassword(userStr, pass.toStdString(), deviceId);
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

    // Persist session — store is already open from main.cpp
    if (store_ && !store_->isOpen()) {
        const char* xdg = getenv("XDG_DATA_HOME");
        std::string dataPath;
        if (xdg && xdg[0]) { dataPath = std::string(xdg) + "/progressive-desktop"; }
        else { const char* home = getenv("HOME"); dataPath = std::string(home ? home : "/tmp") + "/.local/share/progressive-desktop"; }
        std::filesystem::create_directories(dataPath);
        QString dbPath = QString::fromStdString(dataPath + "/session.db");
        bool opened = store_->open(dbPath.toStdString());
        LOG(LogChannel::E2EE, "login: store open ok=%d db=%p",
            opened ? 1 : 0, (void*)store_);
    }
    // setSessionStore already called in main.cpp:244 — skip redundant call
    auto saved = client_->account();
    LOG(LogChannel::E2EE, "SAVE: access=%.8s refresh=%.8s device=%s",
        saved.accessToken.c_str(), saved.refreshToken.c_str(), saved.deviceId.c_str());
    client_->persistSession();

    logged_in_ = true;
    accept();
}

} // namespace progressive::desktop
