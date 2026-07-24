// src/ui/toolbar_handler.cpp — toolbar actions extracted from MainWindow.
#include "toolbar_handler.hpp"
#include "../room/room_store.hpp"
#include "../room_list_model.hpp"
#include "room_handler.hpp"
#include "core/matrix_client.hpp"
#include "../profile/room_members_dialog.hpp"
#include "../profile/user_profile_dialog.hpp"
#include "core/version.h"
#include "../profile_dialog.hpp"
#include "../dialogs/room_settings_dialog.hpp"
#include "../dialogs/room_directory_dialog.hpp"
#include "../dialogs/threads_dialog.hpp"
#include "../dialogs/prefs_dialog.hpp"
#include "../dialogs/network_log_dialog.hpp"
#include "../shared/image_loader.hpp"
#include "../chat/chat_logger.hpp"

#include <chrono>
#include <QInputDialog>
#include <QMessageBox>
#include <QMenu>
#include <QCursor>
#include <QPointer>
#include <QDateTime>
#include <QMainWindow>
#include <filesystem>
#include <cstdlib>
#include "core/thread_pool.hpp"

namespace progressive::desktop {

ToolbarHandler::ToolbarHandler(std::shared_ptr<MatrixClient> client, RoomListModel* roomModel,
                                 RoomStore* roomStore, TimelineModel* timelineModel,
                                 QLabel* statusLabel, QWidget* parent)
    : QObject(parent), client_(std::move(client)), roomModel_(roomModel),
      roomStore_(roomStore), timelineModel_(timelineModel),
      statusLabel_(statusLabel), parentWidget_(parent),
      chatLogger_(std::make_unique<ChatLogger>()) {}

QAction* ToolbarHandler::createNewChatAction() {
    auto* action = new QAction("+ New chat", parentWidget_);
    connect(action, &QAction::triggered, this, &ToolbarHandler::onNewChat);
    return action;
}

QAction* ToolbarHandler::createJoinRoomAction() {
    auto* action = new QAction("Join by ID", parentWidget_);
    connect(action, &QAction::triggered, this, &ToolbarHandler::onJoinRoom);
    return action;
}

QAction* ToolbarHandler::createBrowseRoomsAction() {
    auto* action = new QAction("Browse rooms", parentWidget_);
    connect(action, &QAction::triggered, this, &ToolbarHandler::onBrowseRooms);
    return action;
}

QAction* ToolbarHandler::createAllThreadsAction() {
    auto* action = new QAction("All threads", parentWidget_);
    connect(action, &QAction::triggered, this, &ToolbarHandler::onAllThreads);
    return action;
}

QAction* ToolbarHandler::createRoomSettingsAction() {
    auto* action = new QAction("Room settings", parentWidget_);
    connect(action, &QAction::triggered, this, &ToolbarHandler::onRoomSettings);
    return action;
}

QAction* ToolbarHandler::createRoomMembersAction() {
    auto* action = new QAction("Room members", parentWidget_);
    connect(action, &QAction::triggered, this, &ToolbarHandler::onRoomMembers);
    return action;
}

QAction* ToolbarHandler::createSettingsAction() {
    auto* action = new QAction("Settings", parentWidget_);
    connect(action, &QAction::triggered, this, &ToolbarHandler::onSettings);
    return action;
}

QAction* ToolbarHandler::createFullscreenAction() {
    fullscreenAction_ = new QAction("Fullscreen", parentWidget_);
    connect(fullscreenAction_, &QAction::triggered, this, &ToolbarHandler::doFullscreen);
    return fullscreenAction_;
}

void ToolbarHandler::onNewChat() {
    if (!client_ || !client_->isLoggedIn()) return;
    bool ok;
    QString userId = QInputDialog::getText(parentWidget_, "New direct chat",
        "Enter Matrix user ID (e.g. @bob:matrix.org):", QLineEdit::Normal, "@", &ok);
    if (!ok || userId.trimmed().isEmpty()) return;
    userId = userId.trimmed();
    if (!userId.startsWith("@")) userId = "@" + userId;

    statusLabel_->setText("Creating direct chat...");
    std::string uid = userId.toStdString();
    auto client = client_;
    QPointer<QWidget> guard(parentWidget_);
    ThreadPool::instance().enqueue([guard, client, uid]() {
        auto r = client->startDirectMessage(uid);
        QMetaObject::invokeMethod(guard, [guard, r]() {
            if (guard.isNull()) return;
            if (r.ok) {
                auto* mw = qobject_cast<QWidget*>(guard.data());
                if (mw) {
                    auto* st = mw->findChild<QLabel*>("statusLabel");
                    if (st) st->setText("Created room: " + QString::fromStdString(r.data));
                }
            } else {
                QMessageBox::warning(qobject_cast<QWidget*>(guard.data()), "Error",
                    QString("Failed: %1").arg(QString::fromStdString(r.error.message)));
            }
        }, Qt::QueuedConnection);
    });
}

void ToolbarHandler::onJoinRoom() {
    if (!client_ || !client_->isLoggedIn()) return;
    bool ok;
    QString input = QInputDialog::getText(parentWidget_, "Join room",
        "Enter room ID or alias (e.g. #matrix:matrix.org):", QLineEdit::Normal, "", &ok);
    if (!ok || input.trimmed().isEmpty()) return;
    int hashIdx = input.indexOf("#/#");
    if (hashIdx >= 0) input = input.mid(hashIdx + 2);
    int idIdx = input.indexOf("#/!");
    if (idIdx >= 0) input = input.mid(idIdx + 2);

    statusLabel_->setText("Joining...");
    std::string id = input.trimmed().toStdString();
    auto client = client_;
    QPointer<QWidget> guard(parentWidget_);
    ThreadPool::instance().enqueue([guard, client, id]() {
        auto r = client->joinRoom(id);
        QMetaObject::invokeMethod(guard, [guard, r]() {
            if (guard.isNull()) return;
            if (r.ok) QMessageBox::information(qobject_cast<QWidget*>(guard.data()), "Joined", "Successfully joined room.");
        }, Qt::QueuedConnection);
    });
}

void ToolbarHandler::onBrowseRooms() {
    if (!client_ || !client_->isLoggedIn()) return;
    RoomDirectoryDialog dlg(client_.get(), parentWidget_);
    dlg.exec();
    if (!dlg.joinedRoomId().isEmpty()) {
        RoomData rd;
        rd.roomId = dlg.joinedRoomId().toStdString();
        rd.name = dlg.joinedRoomName().toStdString();
        rd.lastMessage = "Joined";
        rd.lastActivityTs = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        roomModel_->upsertRoom(rd);
        statusLabel_->setText("Joined room: " + dlg.joinedRoomName());
    }
}

void ToolbarHandler::onAllThreads() {
    if (!client_ || !client_->isLoggedIn()) { QMessageBox::information(parentWidget_, "Threads", "Login first."); return; }
    ThreadsDialog dlg(client_.get(), "", parentWidget_);
    dlg.exec();
}

void ToolbarHandler::onRoomSettings() {
    if (!client_) return;
    RoomSettingsDialog dlg(client_.get(), "", "Room", parentWidget_);
    dlg.exec();
}

void ToolbarHandler::onRoomMembers() {
    if (!client_) { QMessageBox::information(parentWidget_, "Members", "Select a room first."); return; }
    RoomMembersDialog dlg(client_.get(), "", parentWidget_);
    dlg.exec();
}

void ToolbarHandler::onSettings() {
    QMenu menu(parentWidget_);
    auto* aboutAction = menu.addAction("About");
    auto* profileAction = menu.addAction("My profile...");
    auto* prefsAction = menu.addAction("Preferences...");
    menu.addSeparator();
    auto* netLogAction = menu.addAction("Network log");
    auto* selected = menu.exec(QCursor::pos());
    if (!selected) return;

    if (selected == aboutAction) {
        QMessageBox::information(parentWidget_, "About",
            "Progressive Chat — Desktop\n\nVersion: " PROGRESSIVE_DESKTOP_VERSION);
    } else if (selected == profileAction) {
        if (!client_ || !client_->isLoggedIn()) return;
        ProfileDialog dlg(client_.get(), parentWidget_);
        dlg.exec();
    } else if (selected == prefsAction) {
        PrefsDialog dlg(parentWidget_);
        dlg.exec();
    } else if (selected == netLogAction) {
        NetworkLogDialog dlg(parentWidget_);
        dlg.exec();
    }
}

void ToolbarHandler::doFullscreen() {
    auto* mw = qobject_cast<QMainWindow*>(parentWidget_);
    if (!mw) return;
    if (!isFullscreen_) {
        mw->showFullScreen();
        isFullscreen_ = true;
        fullscreenAction_->setText("Exit fullscreen");
    } else {
        mw->showNormal();
        isFullscreen_ = false;
        fullscreenAction_->setText("Fullscreen");
    }
}

void ToolbarHandler::onToggleChatLog() {
    if (!roomHandler_ || roomHandler_->currentRoomId().empty()) return;

    if (chatLogger_->active()) {
        chatLogger_->stop();
        chatLogBtn_->setChecked(false);
        chatLogBtn_->setText(" Save");
        statusLabel_->setText("Chat log stopped.");
    } else {
        std::string roomId = roomHandler_->currentRoomId();
        std::string roomName = roomId;  // fallback — room list model has display name
        if (roomModel_) {
            int row = roomModel_->findRowByRoomId(roomId);
            if (row >= 0) {
                auto* rd = roomModel_->at(row);
                if (rd && !rd->name.empty() && rd->name != rd->roomId) roomName = rd->name;
            }
        }
        chatLogger_->start(roomId, roomName);
        if (chatLogger_->active()) {
            chatLogBtn_->setChecked(true);
            chatLogBtn_->setText(" Saving");
            statusLabel_->setText("Chat log started.");
        } else {
            statusLabel_->setText("Failed to create log file.");
        }
    }
}

void ToolbarHandler::toggleThreadPanel() {
    if (!roomHandler_ || roomHandler_->currentRoomId().empty() || !client_) {
        QMessageBox::information(parentWidget_, "Threads", "Select a room first.");
        return;
    }
    ThreadsDialog dlg(client_.get(), roomHandler_->currentRoomId(), parentWidget_);
    dlg.exec();
}

} // namespace progressive::desktop
