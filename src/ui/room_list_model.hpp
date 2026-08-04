// src/ui/room_list_model.hpp — QAbstractListModel for Matrix rooms.
//
// Backed by a vector of RoomData (built from /sync responses).
// The model is updated when SyncEngine emits new syncs — see MainWindow::onSync.

#pragma once

#include <QAbstractListModel>
#include "core/engine/engine_types.hpp"
#include "core/engine/room_state.hpp"
#include <QIcon>
#include <QLabel>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace progressive::desktop {

class RoomListModel : public QAbstractListModel {
    Q_OBJECT

public:
    explicit RoomListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int joinedCount() const;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    // Update or insert a room from a /sync response. Returns true if model changed.
    bool upsertRoom(const RoomData& room);
    void clear();

    // Remove a room by roomId. Returns true if a row was removed.
    bool removeRoom(const std::string& roomId);

    const RoomData* at(int row) const;
    int findRowByRoomId(const std::string& roomId) const;
    void updateHeader(QLabel* header, QLabel* inviteHeader) const;
    void setHeaderLabels(QLabel* chats, QLabel* invites) {
        chatsHeader_ = chats; inviteHeader_ = invites;
    }
    void refreshHeader() const {
        if (chatsHeader_ && inviteHeader_) updateHeader(chatsHeader_, inviteHeader_);
    }

    bool isHidden(const std::string& roomId) const { return state_.isHidden(roomId); }
    void setHiddenRooms(std::unordered_set<std::string> ids) { state_.setHiddenRooms(std::move(ids)); }
    void addHiddenRoom(const std::string& roomId) { state_.hideRoom(roomId); }
    void clearHiddenRoom(const std::string& roomId) { state_.unhideRoom(roomId); }

    enum Roles {
        NameRole = Qt::DisplayRole,
        LastMessageRole = Qt::UserRole + 1,
        RoomIdRole,
        LastSenderRole,
        LastActivityRole,
        UnreadRole,
        IsDirectRole,
        IsEncryptedRole,
        IsSpaceRole,
        AvatarUrlRole,
        IsInviteRole,
        InviterRole,
        TypingUsersRole,
    };

private:
    // The UI-thread copy of the engine's room list (X1 pinned contract).
    RoomState state_;
    QLabel* chatsHeader_ = nullptr;
    QLabel* inviteHeader_ = nullptr;

};

} // namespace progressive::desktop
