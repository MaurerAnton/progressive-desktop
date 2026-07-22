// src/ui/profile_dialog.cpp — edit own display name + avatar.
#include "profile_dialog.hpp"
#include "core/version.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QPixmap>
#include <QImage>
#include <QMetaObject>
#include <QPointer>
#include <QMessageBox>
#include "core/thread_pool.hpp"

#include <simdjson.h>

namespace progressive::desktop {

ProfileDialog::ProfileDialog(MatrixClient* client, QWidget* parent)
    : QDialog(parent), client_(client) {
    setWindowTitle("My Profile");
    setModal(true);
    resize(400, 300);

    nameEdit_ = new QLineEdit(this);
    avatarPreview_ = new QLabel(this);
    avatarPreview_->setFixedSize(80, 80);
    avatarPreview_->setAlignment(Qt::AlignCenter);
    avatarPreview_->setStyleSheet("background:#2a2a2a; border-radius:40px;");
    avatarPreview_->setText("No avatar");

    saveNameBtn_ = new QPushButton("Save name", this);
    setAvatarBtn_ = new QPushButton("Set avatar...", this);
    closeBtn_ = new QPushButton("Close", this);
    statusLabel_ = new QLabel("Loading profile...", this);

    auto* form = new QFormLayout;
    form->addRow("Display name:", nameEdit_);

    auto* avatarRow = new QHBoxLayout;
    avatarRow->addWidget(avatarPreview_);
    avatarRow->addWidget(setAvatarBtn_);
    avatarRow->addStretch();

    auto* btnRow = new QHBoxLayout;
    btnRow->addWidget(saveNameBtn_);
    btnRow->addStretch();
    btnRow->addWidget(closeBtn_);

    auto* root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addLayout(avatarRow);
    root->addLayout(btnRow);
    root->addWidget(statusLabel_);

    connect(saveNameBtn_, &QPushButton::clicked, this, &ProfileDialog::onSaveNameClicked);
    connect(setAvatarBtn_, &QPushButton::clicked, this, &ProfileDialog::onSetAvatarClicked);
    connect(closeBtn_, &QPushButton::clicked, this, &QDialog::accept);

    // Load current profile
    QPointer<ProfileDialog> guard(this);
    std::string userId = client_->account().userId;
    ThreadPool::instance().enqueue([guard, this, userId]() {
        auto r = client_->getProfile(userId);
        QMetaObject::invokeMethod(guard, [guard, r]() {
            if (guard.isNull()) return;
            if (!r.ok) {
                guard->statusLabel_->setText("Failed to load profile: " +
                    QString::fromStdString(r.error.message));
                return;
            }
            // Parse profile JSON: {"displayname":"...","avatar_url":"mxc://..."}
            simdjson::dom::parser parser;
            auto root = parser.parse(r.data);
            if (root.error() == simdjson::SUCCESS) {
                auto dn = root.value()["displayname"].get_string();
                if (dn.error() == simdjson::SUCCESS) {
                    guard->nameEdit_->setText(QString::fromUtf8(dn.value().data(), (int)dn.value().size()));
                }
                auto av = root.value()["avatar_url"].get_string();
                if (av.error() == simdjson::SUCCESS) {
                    guard->currentAvatarMxc_ = std::string(av.value());
                }
            }
            guard->statusLabel_->setText("Ready.");
        }, Qt::QueuedConnection);
    });

    // Pre-fill name from account
    nameEdit_->setText(QString::fromStdString(client_->account().userId));
}

void ProfileDialog::onSaveNameClicked() {
    auto name = nameEdit_->text().toStdString();
    statusLabel_->setText("Saving...");
    QPointer<ProfileDialog> guard(this);
    ThreadPool::instance().enqueue([guard, this, name]() {
        auto r = client_->setDisplayName(name);
        QMetaObject::invokeMethod(guard, [guard, r]() {
            if (guard.isNull()) return;
            if (r.ok) guard->statusLabel_->setText("Display name saved.");
            else guard->statusLabel_->setText("Failed: " + QString::fromStdString(r.error.message));
        }, Qt::QueuedConnection);
    });
}

void ProfileDialog::onSetAvatarClicked() {
    QString path = QFileDialog::getOpenFileName(this, "Select avatar image",
        QString(), "Images (*.png *.jpg *.jpeg *.gif *.webp)");
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        statusLabel_->setText("Failed to open file.");
        return;
    }
    QByteArray data = f.readAll();
    f.close();
    QFileInfo fi(path);
    QMimeDatabase db;
    QString ct = db.mimeTypeForFile(path).name();

    std::vector<uint8_t> bytes(data.begin(), data.end());
    std::string filename = fi.fileName().toStdString();
    std::string contentType = ct.toStdString();

    statusLabel_->setText("Uploading avatar...");
    QPointer<ProfileDialog> guard(this);
    ThreadPool::instance().enqueue([guard, this, bytes, filename, contentType]() {
        auto upload = client_->uploadMedia(bytes, filename, contentType);
        QMetaObject::invokeMethod(guard, [guard, upload]() {
            if (guard.isNull()) return;
            if (!upload.ok) {
                guard->statusLabel_->setText("Upload failed: " + QString::fromStdString(upload.error.message));
                return;
            }
            std::string mxc = upload.data;
            guard->currentAvatarMxc_ = mxc;
            // Now set avatar URL on profile
            ThreadPool::instance().enqueue([guard, client = guard->client_, mxc]() {
                auto r = client->setAvatarUrl(mxc);
                QMetaObject::invokeMethod(guard, [guard, r]() {
                    if (guard.isNull()) return;
                    if (r.ok) guard->statusLabel_->setText("Avatar updated.");
                    else guard->statusLabel_->setText("Set avatar failed: " + QString::fromStdString(r.error.message));
                }, Qt::QueuedConnection);
            });
        }, Qt::QueuedConnection);
    });
}

} // namespace progressive::desktop
