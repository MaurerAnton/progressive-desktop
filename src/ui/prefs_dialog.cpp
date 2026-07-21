// src/ui/prefs_dialog.cpp
#include "prefs_dialog.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFormLayout>

namespace progressive::desktop {

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
    accept();
}

} // namespace progressive::desktop
