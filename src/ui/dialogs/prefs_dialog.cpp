// src/ui/prefs_dialog.cpp
#include "prefs_dialog.hpp"
#include "../handlers/verification_handler.hpp"
#include "core/crypto/decryptor.hpp"
#include "core/crypto/cross_sign.hpp"
#include "core/session_store.hpp"
#include "core/thread_pool.hpp"
#include <map>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QTextStream>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QPointer>
#include <QFormLayout>
#include <QGroupBox>
#include <QScrollArea>
#include "core/matrix_client.hpp"
#include "core/debug_log.hpp"
#include <simdjson.h>

namespace progressive::desktop {

namespace {
inline constexpr int kPrefsW   = 500;
inline constexpr int kPrefsH   = 500;
inline constexpr int kCacheMin = 5;
inline constexpr int kCacheMax = 500;
} // namespace

PrefsDialog::PrefsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Preferences");
    // Measured at the runtime font: longest control text + padding + margins
    // (the PineTab's scaled font clips otherwise — user report Aug 4).
    int minW = fontMetrics().horizontalAdvance("Retrieve secrets with recovery key…") + 80;
    setMinimumWidth(qMax(360, minW));

    // The whole dialog scrolls when the screen is shorter than the content —
    // otherwise the layout squeezes the buttons vertically (clipped text).
    auto* content = new QWidget(this);
    auto* root = new QVBoxLayout(content);
    auto* form = new QFormLayout;

    cacheSpin_ = new QSpinBox(this);
    cacheSpin_->setRange(5, 500);
    cacheSpin_->setValue(imageCacheSize());
    cacheSpin_->setSuffix(" images");
    cacheSpin_->setToolTip("Maximum images cached in RAM. Lower = less memory. Default: 20.");
    form->addRow("Image cache size:", cacheSpin_);

    syncTimeoutSpin_ = new QSpinBox(this);
    syncTimeoutSpin_->setRange(1000, 30000);
    syncTimeoutSpin_->setSingleStep(500);
    syncTimeoutSpin_->setSuffix(" ms");
    syncTimeoutSpin_->setValue(pollTimeoutMs());
    syncTimeoutSpin_->setToolTip(
        "How long /sync waits for new events before returning. "
        "Higher = faster message delivery, fewer requests. Default 20000 (20s).");
    form->addRow("Sync poll timeout:", syncTimeoutSpin_);

    historySpin_ = new QSpinBox(this);
    historySpin_->setRange(20, 100);
    historySpin_->setSingleStep(10);
    historySpin_->setSuffix(" msgs");
    historySpin_->setValue(historyLoadLimit());
    historySpin_->setToolTip("Messages loaded per 'Load more' click. Lower = faster on slow connections. Default 50.");
    form->addRow("History load limit:", historySpin_);

    invisibleCheck_ = new QCheckBox("Suppress read receipts", this);
    invisibleCheck_->setChecked(invisibleMode());
    invisibleCheck_->setToolTip("Don't send read markers. Others won't see when you've read their messages. Default off.");
    form->addRow("Invisible mode:", invisibleCheck_);

    verifiedOnlyCheck_ = new QCheckBox("Share room keys only with verified devices", this);
    verifiedOnlyCheck_->setChecked(shareKeysVerifiedOnly());
    verifiedOnlyCheck_->setToolTip(
        "When enabled, only SAS-verified devices can request room keys from you. "
        "Default off (share with all room members).");
    form->addRow("Key sharing:", verifiedOnlyCheck_);

    auto* securityGroup = new QGroupBox("Security", this);
    auto* securityLayout = new QVBoxLayout(securityGroup);
    xsStatusLabel_ = new QLabel("Cross-signing: not set up", this);
    auto* xsBtn = new QPushButton("Set up secure messaging…", this);
    xsBtn->setToolTip("Generate cross-signing keys (MSK/USK/SSK) and upload them. "
                      "Other devices can then verify this one.");
    auto* xsResetBtn = new QPushButton("Reset cross-signing…", this);
    xsResetBtn->setToolTip("Regenerate the master/self-signing/user-signing keys. "
                           "Existing device signatures become invalid — re-verify "
                           "your devices afterwards.");
    securityLayout->addWidget(xsStatusLabel_);
    securityLayout->addWidget(xsBtn);
    securityLayout->addWidget(xsResetBtn);
    root->addWidget(securityGroup);

    connect(xsBtn, &QPushButton::clicked, this, [this, xsBtn]() {
        if (!setupCrossSigningFn_) return;
        runSecurityAction(setupCrossSigningFn_, xsBtn, "Working…", "Set up secure messaging…",
            "Cross-signing keys generated and uploaded.",
            "Could not set up cross-signing (not logged in, or upload failed).");
    });

    connect(xsResetBtn, &QPushButton::clicked, this, [this, xsResetBtn]() {
        if (QMessageBox::question(this, "Reset cross-signing",
                "This replaces your master/self-signing/user-signing keys. "
                "All device signatures become invalid and other users must "
                "re-verify you. Continue?") != QMessageBox::Yes) return;
        if (!resetCrossSigningFn_) return;
        runSecurityAction(resetCrossSigningFn_, xsResetBtn, "Working…", "Reset cross-signing…",
            "Cross-signing keys regenerated and uploaded.",
            "Could not reset cross-signing (not logged in, or upload failed).");
    });

    auto* backupGroup = new QGroupBox("Key backup", this);
    auto* backupLayout = new QVBoxLayout(backupGroup);
    auto* backupStatus = new QLabel("No backup configured", this);
    auto* createBackupBtn = new QPushButton("Create backup…", this);
    createBackupBtn->setToolTip("Generate a recovery key and create a server-side "
                                "encrypted backup of your room keys.");
    auto* backupNowBtn = new QPushButton("Backup now", this);
    auto* deleteBackupBtn = new QPushButton("Delete backup…", this);
    auto* restoreBtn = new QPushButton("Restore from recovery key…", this);
    auto* ssssUploadBtn = new QPushButton("Sync secrets to my other devices…", this);
    ssssUploadBtn->setToolTip("Encrypt the cross-signing keys to account-data, "
                              "unlockable with the recovery key.");
    auto* ssssRetrieveBtn = new QPushButton("Retrieve secrets with recovery key…", this);
    auto* exportBtn = new QPushButton("Export room keys to file…", this);
    auto* importBtn = new QPushButton("Import room keys from file…", this);
    backupLayout->addWidget(backupStatus);
    backupLayout->addWidget(createBackupBtn);
    backupLayout->addWidget(backupNowBtn);
    backupLayout->addWidget(deleteBackupBtn);
    backupLayout->addWidget(restoreBtn);
    backupLayout->addWidget(ssssUploadBtn);
    backupLayout->addWidget(ssssRetrieveBtn);
    backupLayout->addWidget(exportBtn);
    backupLayout->addWidget(importBtn);
    root->addWidget(backupGroup);

    connect(createBackupBtn, &QPushButton::clicked, this,
            [this, backupStatus, createBackupBtn]() {
        if (!createKeyBackupFn_) return;
        auto guard = QPointer<PrefsDialog>(this);
        auto fn = createKeyBackupFn_;
        createBackupBtn->setEnabled(false);
        createBackupBtn->setText("Working…");
        ThreadPool::instance().enqueue([guard, fn, backupStatus, createBackupBtn]() {
            std::string rk = fn ? fn() : "";
            QMetaObject::invokeMethod(guard, [guard, rk, backupStatus, createBackupBtn]() {
                if (guard.isNull()) return;
                createBackupBtn->setEnabled(true);
                createBackupBtn->setText("Create backup…");
                if (rk.empty()) {
                    QMessageBox::warning(guard, "Key backup",
                        "Could not create the backup (server rejected the version).");
                    return;
                }
                // Show the recovery key ONCE with a confirmation.
                QInputDialog dlg(guard);
                dlg.setWindowTitle("Recovery key — write it down");
                dlg.setLabelText("Store this recovery key somewhere safe. It is shown "
                                 "only once and can restore your room keys on any device:");
                dlg.setTextValue(QString::fromStdString(rk));
                dlg.exec();
                if (QMessageBox::question(guard, "Recovery key",
                        "Did you write down the recovery key? The backup is NOT usable "
                        "without it.") == QMessageBox::Yes) {
                    backupStatus->setText("Backup configured");
                    guard->runAsyncButton(createBackupBtn, "Working…", "Create backup…",
                        [up = guard->uploadKeyBackupFn_]() { return up && up(); },
                        [backupStatus](bool ok) {
                            backupStatus->setText(ok ? "Backup configured and uploaded"
                                                     : "Backup configured (upload failed)");
                        });
                }
            });
        });
    });
    connect(deleteBackupBtn, &QPushButton::clicked, this, [this, backupStatus, deleteBackupBtn]() {
        if (!deleteKeyBackupFn_) return;
        if (QMessageBox::question(this, "Delete backup",
                "Delete the server-side key backup? The recovery key becomes "
                "useless for this account.") != QMessageBox::Yes) return;
        runAsyncButton(deleteBackupBtn, "Working…", "Delete backup…",
            [fn = deleteKeyBackupFn_]() { return fn && fn(); },
            [backupStatus, this](bool ok) {
                if (ok) backupStatus->setText("Backup deleted");
                else QMessageBox::warning(this, "Delete backup", "Delete failed.");
            });
    });
    connect(backupNowBtn, &QPushButton::clicked, this, [this, backupStatus, backupNowBtn]() {
        runAsyncButton(backupNowBtn, "Working…", "Backup now",
            [fn = uploadKeyBackupFn_]() { return fn && fn(); },
            [backupStatus, this](bool ok) {
                if (ok) backupStatus->setText("Backup uploaded");
                else QMessageBox::warning(this, "Key backup",
                    "Upload failed (no backup configured, or the server rejected it).");
            });
    });
    connect(ssssUploadBtn, &QPushButton::clicked, this, [this, backupStatus, ssssUploadBtn]() {
        if (!uploadSsssFn_) return;
        QInputDialog dlg(this);
        dlg.setWindowTitle("Sync secrets to other devices");
        dlg.setLabelText("Enter (or paste) the recovery key used to encrypt the "
                         "cross-signing secrets:");
        dlg.setTextEchoMode(QLineEdit::Normal);
        if (dlg.exec() != QDialog::Accepted || dlg.textValue().isEmpty()) return;
        std::string pw = dlg.textValue().toStdString();
        runAsyncButton(ssssUploadBtn, "Working…", "Sync secrets to my other devices…",
            [fn = uploadSsssFn_, pw]() { return fn && fn(pw); },
            [backupStatus, this](bool ok) {
                if (ok) backupStatus->setText("Secrets synced to account-data");
                else QMessageBox::warning(this, "Secrets sync",
                    "Failed (no cross-signing keys, or the upload was rejected).");
            });
    });
    connect(ssssRetrieveBtn, &QPushButton::clicked, this, [this, backupStatus, ssssRetrieveBtn]() {
        if (!retrieveSsssFn_) return;
        QInputDialog dlg(this);
        dlg.setWindowTitle("Retrieve secrets");
        dlg.setLabelText("Enter the recovery key:");
        dlg.setTextEchoMode(QLineEdit::Normal);
        if (dlg.exec() != QDialog::Accepted || dlg.textValue().isEmpty()) return;
        std::string pw = dlg.textValue().toStdString();
        runAsyncButton(ssssRetrieveBtn, "Working…", "Retrieve secrets with recovery key…",
            [fn = retrieveSsssFn_, pw]() { return fn && fn(pw) > 0; },
            [backupStatus](bool ok) {
                backupStatus->setText(ok
                    ? "Secrets retrieved — device keys re-signed"
                    : "Retrieve failed (wrong key or no synced secrets)");
            });
    });
    connect(restoreBtn, &QPushButton::clicked, this, [this, backupStatus, restoreBtn]() {
        if (!restoreKeyBackupFn_) return;
        QInputDialog dlg(this);
        dlg.setWindowTitle("Restore from recovery key");
        dlg.setLabelText("Enter the recovery key:");
        dlg.setTextEchoMode(QLineEdit::Normal);
        if (dlg.exec() != QDialog::Accepted || dlg.textValue().isEmpty()) return;
        std::string pw = dlg.textValue().toStdString();
        runAsyncButton(restoreBtn, "Working…", "Restore from recovery key…",
            [fn = restoreKeyBackupFn_, pw]() { return fn && fn(pw) > 0; },
            [backupStatus](bool ok) {
                backupStatus->setText(ok
                    ? "Restored sessions"
                    : "Nothing restored (wrong key or no backup found)");
            });
    });

    connect(exportBtn, &QPushButton::clicked, this, [this]() {
        if (!decryptor_) { QMessageBox::information(this, "Key backup", "E2EE not initialized."); return; }
        QString file = QFileDialog::getSaveFileName(this, "Export room keys", "megolm-keys.json");
        if (file.isEmpty()) return;
        QFile f(file);
        if (!f.open(QIODevice::WriteOnly)) {
            QMessageBox::warning(this, "Key backup", "Could not open file for writing.");
            return;
        }
        QTextStream ts(&f);
        ts << QString::fromStdString(decryptor_->exportAllKeys());
        QMessageBox::information(this, "Key backup", "Room keys exported.");
    });
    connect(importBtn, &QPushButton::clicked, this, [this]() {
        if (!decryptor_) { QMessageBox::information(this, "Key backup", "E2EE not initialized."); return; }
        QString file = QFileDialog::getOpenFileName(this, "Import room keys", "megolm-keys.json");
        if (file.isEmpty()) return;
        QFile f(file);
        if (!f.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(this, "Key backup", "Could not open file for reading.");
            return;
        }
        std::string json = f.readAll().toStdString();
        int n = decryptor_->importKeys(json);
        QMessageBox::information(this, "Key backup",
            n > 0 ? QString("Imported %1 session(s).").arg(n)
                  : "No sessions imported (invalid file or no new keys).");
    });

    root->addLayout(form);

    auto* devicesGroup = new QGroupBox("Your devices", this);
    auto* devicesLayout = new QVBoxLayout(devicesGroup);
    auto* devicesScroll = new QScrollArea(this);
    devicesScroll->setWidgetResizable(true);
    auto* scrollWidget = new QWidget(devicesScroll);
    devicesSection_ = new QVBoxLayout(scrollWidget);
    devicesScroll->setWidget(scrollWidget);
    devicesLayout->addWidget(devicesScroll);
    root->addWidget(devicesGroup);
    // loadDevices runs from setClient() — the constructor runs before the
    // client is set (the "Not logged in." bug).

    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    auto* saveBtn = new QPushButton("Save", this);
    auto* cancelBtn = new QPushButton("Cancel", this);
    btnRow->addWidget(saveBtn);
    btnRow->addWidget(cancelBtn);
    root->addLayout(btnRow);

    connect(saveBtn, &QPushButton::clicked, this, &PrefsDialog::onSave);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    auto* outer = new QVBoxLayout(this);
    auto* dialogScroll = new QScrollArea(this);
    dialogScroll->setWidgetResizable(true);
    dialogScroll->setWidget(content);
    outer->addWidget(dialogScroll);
}


void PrefsDialog::runAsyncButton(QPushButton* btn, const QString& busyText,
                                 const QString& idleText, std::function<bool()> fn,
                                 std::function<void(bool)> onDone) {
    btn->setEnabled(false);
    btn->setText(busyText);
    QPointer<PrefsDialog> guard(this);
    ThreadPool::instance().enqueue([guard, btn, busyText, idleText,
                                    fn = std::move(fn), onDone = std::move(onDone)]() {
        bool ok = fn ? fn() : false;
        QMetaObject::invokeMethod(guard, [guard, btn, busyText, idleText, ok, onDone]() {
            if (guard.isNull()) return;
            btn->setEnabled(true);
            btn->setText(idleText);
            if (onDone) onDone(ok);
        });
    });
}

void PrefsDialog::runSecurityAction(std::function<bool()> fn, QPushButton* btn,
                                        const QString& busyText, const QString& okLabel,
                                        const QString& okMsg, const QString& failMsg) {
    btn->setEnabled(false);
    btn->setText(busyText);
    QPointer<PrefsDialog> guard(this);
    auto startFn = std::move(fn);
    ThreadPool::instance().enqueue([guard, startFn, btn, busyText, okLabel, okMsg, failMsg, this]() {
        bool ok = startFn ? startFn() : false;
        QMetaObject::invokeMethod(guard, [guard, btn, busyText, okLabel, okMsg, failMsg, this, ok]() {
            if (guard.isNull()) return;
            btn->setEnabled(true);
            btn->setText(okLabel);
            if (ok) {
                xsStatusLabel_->setText(okLabel);
                QMessageBox::information(this, "Security", okMsg);
                return;
            }
            // UIA challenge (password required by the homeserver)? Retry async too.
            std::string session = uiaSessionFn_ ? uiaSessionFn_() : "";
            if (!session.empty() && setupCrossSigningWithPasswordFn_) {
                QInputDialog dlg(this);
                dlg.setWindowTitle("Confirm password");
                dlg.setLabelText("The homeserver requires password confirmation:");
                dlg.setTextEchoMode(QLineEdit::Password);
                if (dlg.exec() == QDialog::Accepted && !dlg.textValue().isEmpty()) {
                    std::string pw = dlg.textValue().toStdString();
                    btn->setEnabled(false);
                    btn->setText(busyText);
                    auto pwFn = setupCrossSigningWithPasswordFn_;
                    ThreadPool::instance().enqueue([guard, btn, busyText, okLabel, okMsg, failMsg, this, pw, pwFn]() {
                        bool ok2 = pwFn ? pwFn(pw) : false;
                        QMetaObject::invokeMethod(guard, [guard, btn, okLabel, okMsg, failMsg, this, ok2]() {
                            if (guard.isNull()) return;
                            btn->setEnabled(true);
                            btn->setText(okLabel);
                            if (ok2) {
                                xsStatusLabel_->setText(okLabel);
                                QMessageBox::information(this, "Security", okMsg);
                            } else {
                                QMessageBox::warning(this, "Security", failMsg);
                            }
                        });
                    });
                }
                return;
            }
            QMessageBox::warning(this, "Security", failMsg);
        });
    });
}

void PrefsDialog::loadDevices(QVBoxLayout* sectionLayout) {
    // Clear any previous rows (reload-safe).
    while (QLayoutItem* item = sectionLayout->takeAt(0)) {
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }
    if (!client_ || !client_->isLoggedIn()) {
        sectionLayout->addWidget(new QLabel("Not logged in.", this));
        return;
    }
    const std::string ourUserId = client_->account().userId;
    const std::string ourDeviceId = client_->account().deviceId;
    std::string queryBody = "{\"device_keys\":{\"" + ourUserId + "\":[]}}";

    struct DeviceRow {
        std::string deviceId;
        std::string displayName;
        progressive::desktop::DeviceTrust trust;
    };
    QPointer<PrefsDialog> guard(this);
    auto client = client_;
    SessionStore* store = store_;
    sectionLayout->addWidget(new QLabel("Loading devices…", this));
    ThreadPool::instance().enqueue([guard, client, store, ourUserId, ourDeviceId,
                                     queryBody, sectionLayout, this]() {
        std::vector<DeviceRow> rows;
        std::string fetchError;
        auto resp = client->queryKeys(queryBody);
        if (!resp.ok) {
            fetchError = "Could not fetch devices. (HTTP " +
                         std::to_string(resp.httpStatus) + ")";
            LOG(LogChannel::E2EE, "prefs: keys/query FAILED http=%d", resp.httpStatus);
        } else {
            // Trust shields: green = SAS-verified, grey = SSK cross-signed,
            // red = unverified. A user whose master carries OUR USK signature
            // upgrades all their devices to Verified.
            std::string ourUsk = store ? store->loadUserSigningPub(ourUserId) : "";
            std::map<std::string, progressive::desktop::DeviceTrust> trustByDev;
            for (const auto& r : progressive::desktop::computeDeviceTrust(
                    resp.data, ourUserId, ourUserId, ourUsk))
                trustByDev[r.deviceId] = r.trust;
            simdjson::dom::parser p;
            auto doc = p.parse(resp.data);
            if (doc.error() == simdjson::SUCCESS) {
                auto userObj = doc.value()["device_keys"][ourUserId];
                if (userObj.error() == simdjson::SUCCESS) {
                    for (auto dev : userObj.value().get_object().value()) {
                        std::string deviceId(dev.key);
                        if (deviceId == ourDeviceId) continue;  // current device
                        auto devObj = dev.value.get_object();
                        if (devObj.error() != simdjson::SUCCESS) continue;
                        std::string displayName;
                        auto dn = devObj.value()["display_name"].get_string();
                        if (dn.error() == simdjson::SUCCESS) displayName = std::string(dn.value());
                        if (displayName.empty()) displayName = deviceId;
                        DeviceRow row;
                        row.deviceId = deviceId;
                        row.displayName = displayName;
                        auto it = trustByDev.find(deviceId);
                        row.trust = it != trustByDev.end() ? it->second
                            : progressive::desktop::DeviceTrust::Unverified;
                        rows.push_back(std::move(row));
                    }
                }
            }
        }
        QMetaObject::invokeMethod(guard, [guard, sectionLayout, rows, fetchError, this, ourUserId, ourDeviceId]() {
            if (guard.isNull()) return;
            while (QLayoutItem* item = sectionLayout->takeAt(0)) {
                if (QWidget* w = item->widget()) w->deleteLater();
                delete item;
            }
            if (!fetchError.empty()) {
                sectionLayout->addWidget(new QLabel(QString::fromStdString(fetchError), this));
                return;
            }
            if (rows.empty()) {
                sectionLayout->addWidget(new QLabel("No other devices to verify.", this));
            }
            // The current device's status: cross-signing published? device
            // signed by the account's SSK? (answers "why is my device not
            // logged in / verified" in-app).
            bool selfSigned = false;
            for (const auto& row : rows) {
                if (row.deviceId == ourDeviceId && row.trust == progressive::desktop::DeviceTrust::Trusted)
                    selfSigned = true;
            }
            QString selfState;
            QColor selfColor = QColor("#F44336");
            if (selfSigned) {
                selfState = "This device: signed by cross-signing \u2713";
                selfColor = QColor("#4CAF50");
            } else {
                selfState = "This device: NOT cross-signed — re-run setup or check the log viewer (E2EE)";
            }
            auto* selfRow = new QHBoxLayout;
            auto* selfDot = new QLabel("\u25CF", this);
            selfDot->setStyleSheet("color:" + selfColor.name() + "; font-size:14px;");
            auto* selfLabel = new QLabel(selfState, this);
            selfLabel->setWordWrap(true);
            selfRow->addWidget(selfDot);
            selfRow->addWidget(selfLabel, 1);
            auto* selfWidget = new QWidget(this);
            selfWidget->setLayout(selfRow);
            sectionLayout->addWidget(selfWidget);
            if (rows.empty()) return;
            for (const auto& row : rows) {
                bool sasVerified = store_ && store_->isDeviceVerified(ourUserId, row.deviceId);
                bool sskTrusted = row.trust == progressive::desktop::DeviceTrust::Trusted;
                QString shieldColor = sasVerified ? "#4CAF50" : (sskTrusted ? "#9E9E9E" : "#F44336");
                auto* rowL = new QHBoxLayout;
                auto* shield = new QLabel("●", this);
                shield->setStyleSheet("color:" + shieldColor + "; font-size:14px;");
                shield->setToolTip(sasVerified ? "Verified (SAS)" :
                                   (sskTrusted ? "Trusted (cross-signed)" : "Unverified"));
                auto* label = new QLabel(QString::fromStdString(row.displayName), this);
                label->setToolTip(QString::fromStdString(row.deviceId));
                auto* verifyBtn = new QPushButton("Verify", this);
                rowL->addWidget(shield);
                rowL->addWidget(label, 1);
                rowL->addWidget(verifyBtn);
                auto* rowWidget = new QWidget(this);
                rowWidget->setLayout(rowL);
                sectionLayout->addWidget(rowWidget);
                connect(verifyBtn, &QPushButton::clicked, this, [this, deviceId = row.deviceId]() {
                    if (verifyHandler_) {
                        verifyHandler_->startSelfVerification(deviceId);
                        accept();
                    }
                });
            }
        });
    });
}

void PrefsDialog::onSave() {
    QSettings s;
    s.setValue("cache/imageCount", cacheSpin_->value());
    s.setValue("sync/pollTimeoutMs", syncTimeoutSpin_->value());
    s.setValue("sync/historyLoadLimit", historySpin_->value());
    s.setValue("privacy/invisibleMode", invisibleCheck_->isChecked());
    s.setValue("e2ee/shareKeysVerifiedOnly", verifiedOnlyCheck_->isChecked());
    accept();
    emit settingsChanged();
}

} // namespace progressive::desktop
