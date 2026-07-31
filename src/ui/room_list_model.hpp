// src/ui/room_list_model.hpp — QAbstractListModel for Matrix rooms.
//
// Backed by a vector of RoomData (built from /sync responses).
// The model is updated when SyncEngine emits new syncs — see MainWindow::onSync.

#pragma once

#include <QAbstractListModel>
#include <QIcon>
#include <QLabel>
#include <string>
#include <vector>
#include <unordered_map>

namespace progressive::desktop {

struct RoomData {
    std::string roomId;
    std::string name;
    std::string lastMessage;
    std::string lastSender;
    int64_t lastActivityTs = 0;
    int unreadCount = 0;
    int highlightCount = 0;
    bool isDirect = false;
    bool isEncrypted = false;
    bool isSpace = false;
    bool isInvite = false;
    std::string inviterId;
    int memberCount = 0;
    std::string avatarUrl;
    std::string parentId;
    std::vector<std::string> typingUsers;  // users currently typing
    bool stateLoaded = false;  // m.room.encryption state already fetched for this room
};

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

    bool isHidden(const std::string& roomId) const {
        return hiddenRoomIds_.count(roomId) > 0;
    }
    void setHiddenRooms(std::unordered_set<std::string> ids) {
        hiddenRoomIds_ = std::move(ids);
    }
    void addHiddenRoom(const std::string& roomId) {
        hiddenRoomIds_.insert(roomId);
    }
    void clearHiddenRoom(const std::string& roomId) {
        hiddenRoomIds_.erase(roomId);
    }

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
    std::vector<RoomData> rooms_;
    std::unordered_map<std::string, int> index_;  // roomId → row, O(1) lookup
    QLabel* chatsHeader_ = nullptr;
    QLabel* inviteHeader_ = nullptr;
    std::unordered_set<std::string> hiddenRoomIds_;
};

} // namespace progressive::desktop
