// src/ui/room_members_dialog.cpp
#include "room_members_dialog.hpp"
#include "user_profile_dialog.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMetaObject>
#include <QPointer>
#include <QMessageBox>
#include <QThread>
#include <thread>

#include <simdjson.h>
#include <cstdio>

namespace progressive::desktop {

RoomMembersDialog::RoomMembersDialog(MatrixClient* client, const std::string& roomId,
                                       QWidget* parent)
    : QDialog(parent), client_(client), roomId_(roomId) {
    setWindowTitle("Room Members");
    setModal(true);
    resize(380, 500);

    searchEdit_ = new QLineEdit(this);
    searchEdit_->setPlaceholderText("Search members...");
    searchEdit_->setStyleSheet("padding:6px; background:#1a1a1a; border:1px solid #333; color:#ddd; border-radius:4px;");

    list_ = new QListWidget(this);
    list_->setStyleSheet("QListWidget{background:#141414; border:1px solid #333;} "
                         "QListWidget::item{color:#ddd; padding:6px;} "
                         "QListWidget::item:hover{background:#2a2a3a;}");

    statusLabel_ = new QLabel("Loading members...", this);
    statusLabel_->setStyleSheet("color:#888;");
    closeBtn_ = new QPushButton("Close", this);

    auto* root = new QVBoxLayout(this);
    root->addWidget(searchEdit_);
    root->addWidget(list_);
    root->addWidget(statusLabel_);
    root->addWidget(closeBtn_);

    connect(searchEdit_, &QLineEdit::textChanged, this, &RoomMembersDialog::onSearchChanged);
    connect(list_, &QListWidget::itemClicked, this, &RoomMembersDialog::onMemberClicked);
    connect(closeBtn_, &QPushButton::clicked, this, &QDialog::accept);

    loadMembers();
}

void RoomMembersDialog::loadMembers() {
    statusLabel_->setText("Loading members...");
    list_->clear();

    QString search = searchEdit_->text().trimmed().toLower();
    QPointer<RoomMembersDialog> guard(this);

    std::thread([guard, this]() {
        auto r = client_->getRoomMembers(roomId_);
        std::vector<MemberInfo> members;

        if (r.ok) {
            simdjson::dom::parser parser;
            auto root = parser.parse(r.data);
            if (root.error() == simdjson::SUCCESS) {
                auto chunk = root.value()["chunk"].get_array();
                if (chunk.error() == simdjson::SUCCESS) {
                    for (auto evt : chunk.value()) {
                        auto membership = evt["content"]["membership"].get_string();
                        if (membership.error() != simdjson::SUCCESS) continue;
                        std::string memberStr(membership.value());

                        MemberInfo m;
                        m.membership = memberStr;

                        auto content = evt["content"];
                        if (content.error() == simdjson::SUCCESS) {
                            std::string contentStr = simdjson::to_string(content.value());

                            auto sk = evt["state_key"].get_string();
                            if (sk.error() == simdjson::SUCCESS) m.userId = std::string(sk.value());

                            // Extract displayname (simple string search in content JSON)
                            auto dnPos = contentStr.find("\"displayname\":\"");
                            if (dnPos != std::string::npos) {
                                dnPos += 15;
                                auto end = contentStr.find('"', dnPos);
                                if (end != std::string::npos) {
                                    m.displayName = contentStr.substr(dnPos, end - dnPos);
                                }
                            }
                            // Try with space: "displayname": "
                            if (m.displayName.empty()) {
                                auto dnPos2 = contentStr.find("\"displayname\": \"");
                                if (dnPos2 != std::string::npos) {
                                    dnPos2 += 16;
                                    auto end = contentStr.find('"', dnPos2);
                                    if (end != std::string::npos) {
                                        m.displayName = contentStr.substr(dnPos2, end - dnPos2);
                                    }
                                }
                            }

                            auto avPos = contentStr.find("\"avatar_url\":\"");
                            if (avPos != std::string::npos) {
                                avPos += 14;
                                auto end = contentStr.find('"', avPos);
                                if (end != std::string::npos) {
                                    m.avatarUrl = contentStr.substr(avPos, end - avPos);
                                }
                            }
                            if (m.avatarUrl.empty()) {
                                auto avPos2 = contentStr.find("\"avatar_url\": \"");
                                if (avPos2 != std::string::npos) {
                                    avPos2 += 15;
                                    auto end = contentStr.find('"', avPos2);
                                    if (end != std::string::npos) {
                                        m.avatarUrl = contentStr.substr(avPos2, end - avPos2);
                                    }
                                }
                            }
                        }

                        // Sort: joined first, then invite, then leave/ban
                        if (memberStr == "join") {
                            members.push_back(std::move(m));
                        }
                    }
                }
            }
        }

        QMetaObject::invokeMethod(guard, [guard, members]() {
            if (guard.isNull()) return;

            guard->allMembers_ = members;

            QString search = guard->searchEdit_->text().trimmed().toLower();
            guard->list_->clear();
            for (const auto& m : members) {
                QString display = QString::fromStdString(m.displayName.empty() ? m.userId : m.displayName);
                if (!search.isEmpty()) {
                    if (!display.toLower().contains(search) &&
                        !QString::fromStdString(m.userId).toLower().contains(search))
                        continue;
                }
                auto* item = new QListWidgetItem(display);
                item->setData(Qt::UserRole, QString::fromStdString(m.userId));
                item->setToolTip(QString::fromStdString(m.userId));
                guard->list_->addItem(item);
            }
            guard->statusLabel_->setText(QString("%1 members").arg(members.size()));
        }, Qt::QueuedConnection);
    }).detach();
}

void RoomMembersDialog::onSearchChanged() {
    loadMembers();
}

void RoomMembersDialog::onMemberClicked(QListWidgetItem* item) {
    if (!item) return;
    QString userId = item->data(Qt::UserRole).toString();
    UserProfileDialog dlg(client_, roomId_, userId.toStdString(), this);
    dlg.exec();
}

} // namespace progressive::desktop
