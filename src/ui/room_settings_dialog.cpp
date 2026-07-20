// src/ui/room_settings_dialog.cpp
#include "room_settings_dialog.hpp"

#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QMenu>
#include <QAction>
#include <QInputDialog>
#include <QMessageBox>
#include <QMetaObject>
#include <QThread>
#include <thread>

#include <progressive/json_parser.hpp>

namespace progressive::desktop {

RoomSettingsDialog::RoomSettingsDialog(MatrixClient* client, const std::string& roomId,
                                         const std::string& roomName, QWidget* parent)
    : QDialog(parent), client_(client), roomId_(roomId) {
    setWindowTitle(QString("Room Settings — %1").arg(QString::fromStdString(roomName)));
    setModal(true);
    resize(600, 500);

    nameEdit_ = new QLineEdit(QString::fromStdString(roomName), this);
    topicEdit_ = new QTextEdit(this);
    topicEdit_->setMaximumHeight(80);
    topicEdit_->setPlaceholderText("Room topic/description...");

    saveNameBtn_ = new QPushButton("Save name", this);
    saveTopicBtn_ = new QPushButton("Save topic", this);
    refreshMembersBtn_ = new QPushButton("Refresh members", this);
    membersList_ = new QListWidget(this);
    statusLabel_ = new QLabel("Loading...", this);
    closeBtn_ = new QPushButton("Close", this);

    auto* form = new QFormLayout;
    form->addRow("Name:", nameEdit_);
    form->addRow("Topic:", topicEdit_);

    auto* saveRow = new QHBoxLayout;
    saveRow->addWidget(saveNameBtn_);
    saveRow->addWidget(saveTopicBtn_);
    saveRow->addStretch();

    auto* root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addLayout(saveRow);
    root->addWidget(new QLabel("Members:", this));
    root->addWidget(refreshMembersBtn_);
    root->addWidget(membersList_);
    root->addWidget(statusLabel_);
    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    btnRow->addWidget(closeBtn_);
    root->addLayout(btnRow);

    connect(saveNameBtn_, &QPushButton::clicked, this, &RoomSettingsDialog::onSaveNameClicked);
    connect(saveTopicBtn_, &QPushButton::clicked, this, &RoomSettingsDialog::onSaveTopicClicked);
    connect(refreshMembersBtn_, &QPushButton::clicked, this, &RoomSettingsDialog::onRefreshMembersClicked);
    connect(closeBtn_, &QPushButton::clicked, this, &QDialog::accept);
    connect(membersList_, &QListWidget::customContextMenuRequested, this, &RoomSettingsDialog::onMemberContextMenu);
    membersList_->setContextMenuPolicy(Qt::CustomContextMenu);

    // Load topic from current state
    std::thread([this]() {
        auto state = client_->getRoomState(roomId_);
        QMetaObject::invokeMethod(this, [this, state]() {
            if (!state.ok) {
                statusLabel_->setText("Failed to load state: " + QString::fromStdString(state.error.message));
                return;
            }
            // Find m.room.topic
            auto topicPos = state.data.find("\"m.room.topic\"");
            if (topicPos != std::string::npos) {
                auto topic = progressive::parseJsonStringValue(state.data.substr(topicPos), "topic");
                topicEdit_->setPlainText(QString::fromStdString(topic));
            }
            statusLabel_->setText("Ready.");
        }, Qt::QueuedConnection);
    }).detach();

    loadMembers();
}

void RoomSettingsDialog::onSaveTopicClicked() {
    auto topic = topicEdit_->toPlainText().toStdString();
    statusLabel_->setText("Saving topic...");
    QApplication::processEvents();

    std::thread([this, topic]() {
        auto r = client_->setRoomTopic(roomId_, topic);
        QMetaObject::invokeMethod(this, [this, r]() {
            if (r.ok) statusLabel_->setText("Topic saved.");
            else statusLabel_->setText("Failed: " + QString::fromStdString(r.error.message));
        }, Qt::QueuedConnection);
    }).detach();
}

void RoomSettingsDialog::onSaveNameClicked() {
    auto name = nameEdit_->text().toStdString();
    statusLabel_->setText("Saving name...");
    QApplication::processEvents();

    std::thread([this, name]() {
        auto r = client_->setRoomName(roomId_, name);
        QMetaObject::invokeMethod(this, [this, r]() {
            if (r.ok) statusLabel_->setText("Name saved.");
            else statusLabel_->setText("Failed: " + QString::fromStdString(r.error.message));
        }, Qt::QueuedConnection);
    }).detach();
}

void RoomSettingsDialog::loadMembers() {
    statusLabel_->setText("Loading members...");
    membersList_->clear();

    std::thread([this]() {
        auto r = client_->getRoomMembers(roomId_);
        QMetaObject::invokeMethod(this, [this, r]() {
            if (!r.ok) {
                statusLabel_->setText("Failed to load members.");
                return;
            }
            // Parse members — find "chunk" array, extract user_id + displayname + membership
            auto pos = r.data.find("\"chunk\"");
            if (pos == std::string::npos) {
                statusLabel_->setText("No members.");
                return;
            }
            auto arrStart = r.data.find('[', pos);
            auto arrEnd = r.data.find(']', arrStart);
            if (arrStart == std::string::npos) return;

            int count = 0;
            size_t p = arrStart + 1;
            while (p < arrEnd) {
                auto objStart = r.data.find('{', p);
                if (objStart == std::string::npos || objStart >= arrEnd) break;
                int depth = 1;
                size_t objEnd = objStart + 1;
                while (objEnd < arrEnd && depth > 0) {
                    if (r.data[objEnd] == '{') depth++;
                    else if (r.data[objEnd] == '}') depth--;
                    objEnd++;
                }
                auto obj = r.data.substr(objStart, objEnd - objStart);
                auto userId = progressive::parseJsonStringValue(obj, "user_id");
                auto displayname = progressive::parseJsonStringValue(obj, "displayname");
                auto membership = progressive::parseJsonStringValue(obj, "membership");

                if (membership == "join" || membership.empty()) {
                    QString display = QString::fromStdString(displayname.empty() ? userId : displayname);
                    display += "  (" + QString::fromStdString(userId) + ")";
                    auto* item = new QListWidgetItem(display);
                    item->setData(Qt::UserRole, QString::fromStdString(userId));
                    membersList_->addItem(item);
                    count++;
                }
                p = objEnd;
            }
            statusLabel_->setText(QString("%1 members.").arg(count));
        }, Qt::QueuedConnection);
    }).detach();
}

void RoomSettingsDialog::onRefreshMembersClicked() {
    loadMembers();
}

void RoomSettingsDialog::onMemberContextMenu(const QPoint& pos) {
    auto* item = membersList_->itemAt(pos);
    if (!item) return;
    QString userId = item->data(Qt::UserRole).toString();
    auto globalPos = membersList_->mapToGlobal(pos);
    showMemberMenu(userId, globalPos);
}

void RoomSettingsDialog::showMemberMenu(const QString& userId, const QPoint& globalPos) {
    QMenu menu(this);

    auto* kickAction = menu.addAction("Kick user");
    auto* banAction = menu.addAction("Ban user");
    menu.addSeparator();
    auto* makeModAction = menu.addAction("Make moderator (level 50)");
    auto* makeAdminAction = menu.addAction("Make admin (level 100)");
    menu.addSeparator();
    auto* resetLevelAction = menu.addAction("Reset to default (level 0)");

    auto* selected = menu.exec(globalPos);
    if (!selected) return;

    auto uid = userId.toStdString();

    if (selected == kickAction) {
        bool ok;
        auto reason = QInputDialog::getText(this, "Kick user", "Reason:", QLineEdit::Normal, "", &ok);
        if (!ok) return;
        std::thread([this, uid, reason = reason.toStdString()]() {
            client_->kickUser(roomId_, uid, reason);
            QMetaObject::invokeMethod(this, [this]() { loadMembers(); }, Qt::QueuedConnection);
        }).detach();
    } else if (selected == banAction) {
        bool ok;
        auto reason = QInputDialog::getText(this, "Ban user", "Reason:", QLineEdit::Normal, "", &ok);
        if (!ok) return;
        std::thread([this, uid, reason = reason.toStdString()]() {
            client_->banUser(roomId_, uid, reason);
            QMetaObject::invokeMethod(this, [this]() { loadMembers(); }, Qt::QueuedConnection);
        }).detach();
    } else if (selected == makeModAction) {
        std::thread([this, uid]() {
            client_->setUserPowerLevel(roomId_, uid, 50);
            QMetaObject::invokeMethod(this, [this]() { loadMembers(); }, Qt::QueuedConnection);
        }).detach();
    } else if (selected == makeAdminAction) {
        std::thread([this, uid]() {
            client_->setUserPowerLevel(roomId_, uid, 100);
            QMetaObject::invokeMethod(this, [this]() { loadMembers(); }, Qt::QueuedConnection);
        }).detach();
    } else if (selected == resetLevelAction) {
        std::thread([this, uid]() {
            client_->setUserPowerLevel(roomId_, uid, 0);
            QMetaObject::invokeMethod(this, [this]() { loadMembers(); }, Qt::QueuedConnection);
        }).detach();
    }
}

} // namespace progressive::desktop
