// src/ui/user_profile_dialog.cpp
#include "user_profile_dialog.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QMetaObject>
#include <QPointer>
#include <QMessageBox>
#include <QClipboard>
#include <QGuiApplication>
#include <QInputDialog>
#include <QThread>
#include <QPixmap>
#include <thread>

#include <simdjson.h>
#include <progressive/json_parser.hpp>
#include <cstdio>

namespace progressive::desktop {

UserProfileDialog::UserProfileDialog(MatrixClient* client, const std::string& roomId,
                                       const std::string& userId, QWidget* parent)
    : QDialog(parent), client_(client), roomId_(roomId), userId_(userId) {
    setWindowTitle("User Profile");
    setModal(true);
    setMinimumWidth(340);

    avatarLabel_ = new QLabel(this);
    avatarLabel_->setFixedSize(72, 72);
    avatarLabel_->setAlignment(Qt::AlignCenter);
    avatarLabel_->setStyleSheet("background:#2a2a2a; border-radius:36px; color:#888;");

    nameLabel_ = new QLabel("...", this);
    nameLabel_->setStyleSheet("font-size:14pt; font-weight:bold; color:#eee;");
    idLabel_ = new QLabel("", this);
    idLabel_->setStyleSheet("color:#888; font-size:10pt;");
    powerLabel_ = new QLabel("", this);
    powerLabel_->setStyleSheet("color:#888; font-size:10pt;");

    dmBtn_ = new QPushButton("Send message", this);
    kickBtn_ = new QPushButton("Kick", this);
    kickBtn_->setStyleSheet("color:#f88;");
    banBtn_ = new QPushButton("Ban", this);
    banBtn_->setStyleSheet("color:#f44;");
    promoteBtn_ = new QPushButton("Promote", this);
    demoteBtn_ = new QPushButton("Demote", this);
    copyBtn_ = new QPushButton("Copy MXID", this);
    closeBtn_ = new QPushButton("Close", this);

    statusLabel_ = new QLabel("", this);
    statusLabel_->setStyleSheet("color:#888; font-size:10pt;");

    auto* infoRow = new QHBoxLayout;
    infoRow->addWidget(avatarLabel_);
    auto* infoCol = new QVBoxLayout;
    infoCol->addWidget(nameLabel_);
    infoCol->addWidget(idLabel_);
    infoCol->addWidget(powerLabel_);
    infoCol->addStretch();
    infoRow->addLayout(infoCol);

    auto* actionGrid = new QHBoxLayout;
    actionGrid->addWidget(dmBtn_);
    actionGrid->addWidget(kickBtn_);
    actionGrid->addWidget(banBtn_);

    auto* powerRow = new QHBoxLayout;
    powerRow->addWidget(promoteBtn_);
    powerRow->addWidget(demoteBtn_);
    powerRow->addStretch();

    auto* bottomRow = new QHBoxLayout;
    bottomRow->addWidget(copyBtn_);
    bottomRow->addStretch();
    bottomRow->addWidget(closeBtn_);

    auto* root = new QVBoxLayout(this);
    root->addLayout(infoRow);
    root->addSpacing(8);
    root->addLayout(actionGrid);
    root->addSpacing(4);
    root->addLayout(powerRow);
    root->addSpacing(4);
    root->addLayout(bottomRow);
    root->addWidget(statusLabel_);

    connect(dmBtn_, &QPushButton::clicked, this, &UserProfileDialog::onSendDM);
    connect(kickBtn_, &QPushButton::clicked, this, &UserProfileDialog::onKick);
    connect(banBtn_, &QPushButton::clicked, this, &UserProfileDialog::onBan);
    connect(promoteBtn_, &QPushButton::clicked, this, &UserProfileDialog::onPromote);
    connect(demoteBtn_, &QPushButton::clicked, this, &UserProfileDialog::onDemote);
    connect(copyBtn_, &QPushButton::clicked, this, &UserProfileDialog::onCopyMXID);
    connect(closeBtn_, &QPushButton::clicked, this, &QDialog::accept);

    idLabel_->setText(QString::fromStdString(userId_));
    loadProfile();
}

void UserProfileDialog::loadProfile() {
    QPointer<UserProfileDialog> guard(this);
    std::string userId = userId_;
    std::string roomId = roomId_;

    statusLabel_->setText("Loading...");

    std::thread([guard, userId, roomId]() {
        // Fetch user profile + room member state
        auto profileResp = guard->client_->getUserProfile(userId);
        auto membersResp = guard->client_->getRoomMembers(roomId);

        QMetaObject::invokeMethod(guard, [guard, profileResp, membersResp, userId]() {
            if (guard.isNull()) return;

            // Profile
            if (profileResp.ok && !profileResp.data.empty()) {
                simdjson::dom::parser parser;
                auto root = parser.parse(profileResp.data);
                if (root.error() == simdjson::SUCCESS) {
                    auto dn = root.value()["displayname"].get_string();
                    if (dn.error() == simdjson::SUCCESS) {
                        guard->nameLabel_->setText(QString::fromUtf8(dn.value().data(), (int)dn.value().size()));
                    } else {
                        // Fallback: extract localpart
                        std::string uid = userId;
                        if (uid[0] == '@') {
                            auto colon = uid.find(':');
                            if (colon != std::string::npos) uid = uid.substr(1, colon - 1);
                            else uid = uid.substr(1);
                        }
                        guard->nameLabel_->setText(QString::fromStdString(uid));
                    }
                    auto av = root.value()["avatar_url"].get_string();
                    if (av.error() == simdjson::SUCCESS) {
                        std::string avUrl(av.value());
                        // Download avatar
                        std::thread([guard, avUrl]() {
                            auto r = guard->client_->downloadMedia(avUrl, 72, 72);
                            QMetaObject::invokeMethod(guard, [guard, r]() {
                                if (guard.isNull() || !r.ok || r.data.empty()) return;
                                QPixmap pix;
                                pix.loadFromData(r.data.data(), (int)r.data.size());
                                if (!pix.isNull()) {
                                    guard->avatarLabel_->setPixmap(pix.scaled(72, 72, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                                }
                            });
                        }).detach();
                    }
                }
            }

            // Power level from room state
            if (membersResp.ok && !membersResp.data.empty()) {
                simdjson::dom::parser parser;
                auto root = parser.parse(membersResp.data);
                if (root.error() == simdjson::SUCCESS) {
                    auto chunk = root.value()["chunk"].get_array();
                    if (chunk.error() == simdjson::SUCCESS) {
                        for (auto evt : chunk.value()) {
                            auto sk = evt["state_key"].get_string();
                            if (sk.error() != simdjson::SUCCESS ||
                                std::string(sk.value()) != userId) continue;
                            auto content = evt["content"];
                            if (content.error() != simdjson::SUCCESS) continue;
                            std::string contentStr = simdjson::to_string(content.value());

                            // Check for power level in content or use default
                            // The actual power level is in m.room.power_levels, not member event.
                            // For simplicity, just note this is a joined member.
                            auto membership = evt["content"]["membership"].get_string();
                            if (membership.error() == simdjson::SUCCESS &&
                                std::string(membership.value()) == "join") {
                                // We're joined — extract power level from power_levels if parsed
                            }
                        }
                    }
                }
            }

            guard->statusLabel_->setText("");
        }, Qt::QueuedConnection);
    }).detach();
}

void UserProfileDialog::onSendDM() {
    QPointer<UserProfileDialog> guard(this);
    statusLabel_->setText("Creating DM...");

    std::thread([guard, this]() {
        auto r = client_->startDirectMessage(userId_);
        QMetaObject::invokeMethod(guard, [guard, r]() {
            if (guard.isNull()) return;
            if (r.ok) {
                guard->statusLabel_->setText("DM created: " + QString::fromStdString(r.data));
            } else {
                guard->statusLabel_->setText("Failed: " + QString::fromStdString(r.error.message));
            }
        }, Qt::QueuedConnection);
    }).detach();
}

void UserProfileDialog::onKick() {
    auto reply = QMessageBox::question(this, "Kick user",
        QString("Kick %1?").arg(nameLabel_->text()),
        QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes) return;

    statusLabel_->setText("Kicking...");
    QPointer<UserProfileDialog> guard(this);

    std::thread([guard, this]() {
        auto r = client_->kickUser(roomId_, userId_);
        QMetaObject::invokeMethod(guard, [guard, r]() {
            if (guard.isNull()) return;
            if (r.ok) guard->statusLabel_->setText("User kicked.");
            else guard->statusLabel_->setText("Failed: " + QString::fromStdString(r.error.message));
        }, Qt::QueuedConnection);
    }).detach();
}

void UserProfileDialog::onBan() {
    bool ok;
    QString reason = QInputDialog::getText(this, "Ban user",
        "Reason (optional):", QLineEdit::Normal, "", &ok);
    if (!ok) return;

    statusLabel_->setText("Banning...");
    QPointer<UserProfileDialog> guard(this);

    std::thread([guard, this, reason]() {
        auto r = client_->banUser(roomId_, userId_, reason.toStdString());
        QMetaObject::invokeMethod(guard, [guard, r]() {
            if (guard.isNull()) return;
            if (r.ok) guard->statusLabel_->setText("User banned.");
            else guard->statusLabel_->setText("Failed: " + QString::fromStdString(r.error.message));
        }, Qt::QueuedConnection);
    }).detach();
}

void UserProfileDialog::onPromote() {
    statusLabel_->setText("Promoting...");
    QPointer<UserProfileDialog> guard(this);

    std::thread([guard, this]() {
        auto r = client_->setUserPowerLevel(roomId_, userId_, 50);
        QMetaObject::invokeMethod(guard, [guard, r]() {
            if (guard.isNull()) return;
            if (r.ok) guard->statusLabel_->setText("User promoted to moderator.");
            else guard->statusLabel_->setText("Failed: " + QString::fromStdString(r.error.message));
        }, Qt::QueuedConnection);
    }).detach();
}

void UserProfileDialog::onDemote() {
    statusLabel_->setText("Demoting...");
    QPointer<UserProfileDialog> guard(this);

    std::thread([guard, this]() {
        auto r = client_->setUserPowerLevel(roomId_, userId_, 0);
        QMetaObject::invokeMethod(guard, [guard, r]() {
            if (guard.isNull()) return;
            if (r.ok) guard->statusLabel_->setText("User demoted.");
            else guard->statusLabel_->setText("Failed: " + QString::fromStdString(r.error.message));
        }, Qt::QueuedConnection);
    }).detach();
}

void UserProfileDialog::onCopyMXID() {
    QGuiApplication::clipboard()->setText(QString::fromStdString(userId_));
    statusLabel_->setText("MXID copied!");
}

} // namespace progressive::desktop
