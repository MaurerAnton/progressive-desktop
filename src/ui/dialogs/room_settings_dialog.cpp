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
#include <QPointer>
#include <QThread>
#include "core/thread_pool.hpp"

#include <progressive/json_parser.hpp>
#include <simdjson.h>

namespace progressive::desktop {

namespace {
inline constexpr int kSettingsW   = 600;
inline constexpr int kSettingsH   = 500;
inline constexpr int kTopicMaxH   = 80;
} // namespace

namespace {

// Parse members response using simdjson — safe, fast, no string scan crashes.
struct MemberInfo {
    std::string userId;
    std::string displayname;
    std::string membership;
    std::string avatarUrl;
};

std::vector<MemberInfo> parseMembersResponse(const std::string& json) {
    std::vector<MemberInfo> result;
    simdjson::dom::parser parser;
    auto rootResult = parser.parse(json);
    if (rootResult.error() != simdjson::SUCCESS) return result;

    auto chunkResult = rootResult.value()["chunk"].get_array();
    if (chunkResult.error() != simdjson::SUCCESS) return result;

    for (auto evt : chunkResult.value()) {
        MemberInfo m;
        // /members API returns m.room.member state events. User ID is in
        // "state_key" (NOT "user_id" — that field doesn't exist). The
        // content object holds membership, displayname, avatar_url.
        auto uid = evt["state_key"].get_string();
        if (uid.error() == simdjson::SUCCESS) m.userId = std::string(uid.value());

        auto dn = evt["content"]["displayname"].get_string();
        if (dn.error() == simdjson::SUCCESS) m.displayname = std::string(dn.value());

        auto memb = evt["content"]["membership"].get_string();
        if (memb.error() == simdjson::SUCCESS) m.membership = std::string(memb.value());

        auto av = evt["content"]["avatar_url"].get_string();
        if (av.error() == simdjson::SUCCESS) m.avatarUrl = std::string(av.value());

        if (!m.userId.empty()) result.push_back(std::move(m));
    }
    return result;
}

} // namespace

RoomSettingsDialog::RoomSettingsDialog(MatrixClient* client, const std::string& roomId,
                                         const std::string& roomName, QWidget* parent)
    : QDialog(parent), client_(client), roomId_(roomId) {
    setWindowTitle(QString("Room Settings — %1").arg(QString::fromStdString(roomName)));
    setModal(true);
    resize(kSettingsW, kSettingsH);

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

    // Load topic from current state using simdjson
    QPointer<RoomSettingsDialog> guard(this);
    ThreadPool::instance().enqueue([guard, this]() {
        auto state = client_->getRoomState(roomId_);
        QMetaObject::invokeMethod(guard, [guard, state]() {
            if (guard.isNull()) return;
            if (!state.ok) {
                guard->statusLabel_->setText("Failed to load state: " + QString::fromStdString(state.error.message));
                return;
            }
            // Parse state events with simdjson — state.data is an array of events
            simdjson::dom::parser parser;
            auto rootResult = parser.parse(state.data);
            if (rootResult.error() == simdjson::SUCCESS) {
                // State endpoint returns an array of event objects
                auto arrResult = rootResult.value().get_array();
                if (arrResult.error() == simdjson::SUCCESS) {
                    for (auto evt : arrResult.value()) {
                        auto t = evt["type"].get_string();
                        if (t.error() == simdjson::SUCCESS && t.value() == "m.room.topic") {
                            auto topic = evt["content"]["topic"].get_string();
                            if (topic.error() == simdjson::SUCCESS) {
                                guard->topicEdit_->setPlainText(QString::fromUtf8(topic.value().data(), (int)topic.value().size()));
                            }
                        }
                    }
                }
            }
            guard->statusLabel_->setText("Ready.");
        }, Qt::QueuedConnection);
    });

    loadMembers();
}

void RoomSettingsDialog::onSaveTopicClicked() {
    auto topic = topicEdit_->toPlainText().toStdString();
    statusLabel_->setText("Saving topic...");
    QApplication::processEvents();

    QPointer<RoomSettingsDialog> guard(this);
    ThreadPool::instance().enqueue([guard, this, topic]() {
        auto r = client_->setRoomTopic(roomId_, topic);
        QMetaObject::invokeMethod(guard, [guard, r]() {
            if (guard.isNull()) return;
            if (r.ok) guard->statusLabel_->setText("Topic saved.");
            else guard->statusLabel_->setText("Failed: " + QString::fromStdString(r.error.message));
        }, Qt::QueuedConnection);
    });
}

void RoomSettingsDialog::onSaveNameClicked() {
    auto name = nameEdit_->text().toStdString();
    statusLabel_->setText("Saving name...");
    QApplication::processEvents();

    QPointer<RoomSettingsDialog> guard(this);
    ThreadPool::instance().enqueue([guard, this, name]() {
        auto r = client_->setRoomName(roomId_, name);
        QMetaObject::invokeMethod(guard, [guard, r]() {
            if (guard.isNull()) return;
            if (r.ok) guard->statusLabel_->setText("Name saved.");
            else guard->statusLabel_->setText("Failed: " + QString::fromStdString(r.error.message));
        }, Qt::QueuedConnection);
    });
}

void RoomSettingsDialog::loadMembers() {
    statusLabel_->setText("Loading members...");
    membersList_->clear();

    QPointer<RoomSettingsDialog> guard(this);
    ThreadPool::instance().enqueue([guard, this]() {
        auto r = client_->getRoomMembers(roomId_);
        QMetaObject::invokeMethod(guard, [guard, r]() {
            if (guard.isNull()) return;
            if (!r.ok) {
                guard->statusLabel_->setText("Failed to load members: " + QString::fromStdString(r.error.message));
                return;
            }

            auto members = parseMembersResponse(r.data);
            if (members.empty()) {
                guard->statusLabel_->setText("No members found.");
                return;
            }

            int count = 0;
            for (const auto& m : members) {
                if (m.membership != "join" && !m.membership.empty()) continue;
                QString display = QString::fromStdString(m.displayname.empty() ? m.userId : m.displayname);
                display += "  (" + QString::fromStdString(m.userId) + ")";
                auto* item = new QListWidgetItem(display);
                item->setData(Qt::UserRole, QString::fromStdString(m.userId));
                guard->membersList_->addItem(item);
                count++;
            }
            guard->statusLabel_->setText(QString("%1 members.").arg(count));
        }, Qt::QueuedConnection);
    });
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
        ThreadPool::instance().enqueue([this, uid, reason = reason.toStdString()]() {
            client_->kickUser(roomId_, uid, reason);
            QMetaObject::invokeMethod(this, [this]() { loadMembers(); }, Qt::QueuedConnection);
        });
    } else if (selected == banAction) {
        bool ok;
        auto reason = QInputDialog::getText(this, "Ban user", "Reason:", QLineEdit::Normal, "", &ok);
        if (!ok) return;
        ThreadPool::instance().enqueue([this, uid, reason = reason.toStdString()]() {
            client_->banUser(roomId_, uid, reason);
            QMetaObject::invokeMethod(this, [this]() { loadMembers(); }, Qt::QueuedConnection);
        });
    } else if (selected == makeModAction) {
        ThreadPool::instance().enqueue([this, uid]() {
            client_->setUserPowerLevel(roomId_, uid, 50);
            QMetaObject::invokeMethod(this, [this]() { loadMembers(); }, Qt::QueuedConnection);
        });
    } else if (selected == makeAdminAction) {
        ThreadPool::instance().enqueue([this, uid]() {
            client_->setUserPowerLevel(roomId_, uid, 100);
            QMetaObject::invokeMethod(this, [this]() { loadMembers(); }, Qt::QueuedConnection);
        });
    } else if (selected == resetLevelAction) {
        ThreadPool::instance().enqueue([this, uid]() {
            client_->setUserPowerLevel(roomId_, uid, 0);
            QMetaObject::invokeMethod(this, [this]() { loadMembers(); }, Qt::QueuedConnection);
        });
    }
}

} // namespace progressive::desktop
