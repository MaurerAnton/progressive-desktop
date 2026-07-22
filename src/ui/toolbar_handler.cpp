// src/ui/toolbar_handler.cpp — toolbar actions extracted from MainWindow.
#include "toolbar_handler.hpp"
#include "room/room_store.hpp"
#include "room_list_model.hpp"
#include "core/matrix_client.hpp"
#include "profile/room_members_dialog.hpp"
#include "profile/user_profile_dialog.hpp"
#include "core/version.h"
#include "profile_dialog.hpp"
#include "dialogs/room_settings_dialog.hpp"
#include "dialogs/room_directory_dialog.hpp"
#include "dialogs/threads_dialog.hpp"
#include "dialogs/prefs_dialog.hpp"
#include "dialogs/network_log_dialog.hpp"
#include "shared/image_loader.hpp"

#include <QInputDialog>
#include <QMessageBox>
#include <QMenu>
#include <QCursor>
#include <QPointer>
#include <thread>

namespace progressive::desktop {

ToolbarHandler::ToolbarHandler(MatrixClient* client, RoomListModel* roomModel,
                                 RoomStore* roomStore, TimelineModel* timelineModel,
                                 QLabel* statusLabel, QWidget* parent)
    : QObject(parent), client_(client), roomModel_(roomModel),
      roomStore_(roomStore), timelineModel_(timelineModel),
      statusLabel_(statusLabel), parentWidget_(parent) {}

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
    MatrixClient* client = client_;
    QPointer<QWidget> guard(parentWidget_);
    std::thread([guard, client, uid]() {
        auto r = client->startDirectMessage(uid);
        QMetaObject::invokeMethod(guard, [guard, r]() {
            if (guard.isNull()) return;
            if (r.ok) {
                auto* st = qobject_cast<QLabel*>(guard->findChild<QLabel*>());
            }
        }, Qt::QueuedConnection);
    }).detach();
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
    MatrixClient* client = client_;
    QPointer<QWidget> guard(parentWidget_);
    std::thread([guard, client, id]() {
        auto r = client->joinRoom(id);
        QMetaObject::invokeMethod(guard, [guard, r]() {
            if (guard.isNull()) return;
            if (r.ok) QMessageBox::information(qobject_cast<QWidget*>(guard), "Joined", "Successfully joined.");
        }, Qt::QueuedConnection);
    }).detach();
}

void ToolbarHandler::onBrowseRooms() {
    if (!client_ || !client_->isLoggedIn()) return;
    RoomDirectoryDialog dlg(client_, parentWidget_);
    dlg.exec();
    if (!dlg.joinedRoomId().isEmpty()) {
        RoomData rd;
        rd.roomId = dlg.joinedRoomId().toStdString();
        rd.name = dlg.joinedRoomName().toStdString();
        rd.lastMessage = "Joined";
        rd.lastActivityTs = static_cast<int64_t>(QDateTime::currentMSecsSinceEpoch());
        roomModel_->upsertRoom(rd);
        statusLabel_->setText("Joined room: " + dlg.joinedRoomName());
    }
}

void ToolbarHandler::onAllThreads() {
    if (!client_ || !client_->isLoggedIn()) { QMessageBox::information(parentWidget_, "Threads", "Login first."); return; }
    ThreadsDialog dlg(client_, "", parentWidget_);
    dlg.exec();
}

void ToolbarHandler::onRoomSettings() {
    if (!client_) return;
    RoomSettingsDialog dlg(client_, "", "Room", parentWidget_);
    dlg.exec();
}

void ToolbarHandler::onRoomMembers() {
    if (!client_) { QMessageBox::information(parentWidget_, "Members", "Select a room first."); return; }
    RoomMembersDialog dlg(client_, "", parentWidget_);
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
        ProfileDialog dlg(client_, parentWidget_);
        dlg.exec();
    } else if (selected == prefsAction) {
        PrefsDialog dlg(parentWidget_);
        dlg.exec();
    } else if (selected == netLogAction) {
        NetworkLogDialog dlg(parentWidget_);
        dlg.exec();
    }
}

} // namespace progressive::desktop
