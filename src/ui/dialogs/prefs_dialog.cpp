// src/ui/prefs_dialog.cpp
#include "prefs_dialog.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFormLayout>

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

    root->addLayout(form);

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

void PrefsDialog::onSave() {
    QSettings s;
    s.setValue("cache/imageCount", cacheSpin_->value());
    s.setValue("sync/pollTimeoutMs", syncTimeoutSpin_->value());
    s.setValue("sync/historyLoadLimit", historySpin_->value());
    s.setValue("privacy/invisibleMode", invisibleCheck_->isChecked());
    accept();
    emit settingsChanged();
}

} // namespace progressive::desktop
