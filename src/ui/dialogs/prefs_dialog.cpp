// src/ui/prefs_dialog.cpp
#include "prefs_dialog.hpp"
#include "../handlers/verification_handler.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
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
    setMinimumWidth(360);

    auto* root = new QVBoxLayout(this);
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
        "Lower = faster message delivery but more requests. Default 3000 (3s).");
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

    root->addLayout(form);

    auto* devicesGroup = new QGroupBox("Your devices", this);
    auto* devicesLayout = new QVBoxLayout(devicesGroup);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto* scrollWidget = new QWidget(scroll);
    auto* sectionLayout = new QVBoxLayout(scrollWidget);
    scroll->setWidget(scrollWidget);
    devicesLayout->addWidget(scroll);
    root->addWidget(devicesGroup);
    loadDevices(sectionLayout);

    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    auto* saveBtn = new QPushButton("Save", this);
    auto* cancelBtn = new QPushButton("Cancel", this);
    btnRow->addWidget(saveBtn);
    btnRow->addWidget(cancelBtn);
    root->addLayout(btnRow);

    connect(saveBtn, &QPushButton::clicked, this, &PrefsDialog::onSave);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
}

void PrefsDialog::loadDevices(QVBoxLayout* sectionLayout) {
    if (!client_ || !client_->isLoggedIn()) {
        sectionLayout->addWidget(new QLabel("Not logged in.", this));
        return;
    }
    const std::string ourUserId = client_->account().userId;
    const std::string ourDeviceId = client_->account().deviceId;

    std::string queryBody = "{\"device_keys\":{\"" + ourUserId + "\":[]}}";
    auto resp = client_->queryKeys(queryBody);
    if (!resp.ok) {
        sectionLayout->addWidget(new QLabel("Could not fetch devices.", this));
        LOG(LogChannel::E2EE, "prefs: keys/query FAILED http=%d", resp.httpStatus);
        return;
    }

    simdjson::dom::parser p;
    auto doc = p.parse(resp.data);
    if (doc.error() != simdjson::SUCCESS) {
        sectionLayout->addWidget(new QLabel("Could not parse devices.", this));
        return;
    }
    auto userObj = doc.value()["device_keys"][ourUserId];
    if (userObj.error() != simdjson::SUCCESS) {
        sectionLayout->addWidget(new QLabel("No device keys found.", this));
        return;
    }
    int count = 0;
    for (auto dev : userObj.value().get_object().value()) {
        std::string deviceId(dev.key);
        if (deviceId == ourDeviceId) continue;  // current device — nothing to verify

        auto devObj = dev.value.get_object();
        if (devObj.error() != simdjson::SUCCESS) continue;

        std::string displayName;
        auto dn = devObj.value()["display_name"].get_string();
        if (dn.error() == simdjson::SUCCESS) displayName = std::string(dn.value());
        if (displayName.empty()) displayName = deviceId;

        auto* row = new QHBoxLayout;
        auto* label = new QLabel(QString::fromStdString(displayName), this);
        label->setToolTip(QString::fromStdString(deviceId));
        auto* verifyBtn = new QPushButton("Verify", this);
        row->addWidget(label, 1);
        row->addWidget(verifyBtn);
        auto* rowWidget = new QWidget(this);
        rowWidget->setLayout(row);
        sectionLayout->addWidget(rowWidget);

        connect(verifyBtn, &QPushButton::clicked, this, [this, deviceId]() {
            if (verifyHandler_) {
                verifyHandler_->startSelfVerification(deviceId);
                accept();
            }
        });
        count++;
    }
    if (count == 0)
        sectionLayout->addWidget(new QLabel("No other devices to verify.", this));
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
