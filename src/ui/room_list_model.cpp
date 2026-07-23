// src/ui/room_list_model.cpp

#include "room_list_model.hpp"

#include <QFontMetrics>
#include <algorithm>

namespace progressive::desktop {

RoomListModel::RoomListModel(QObject* parent)
    : QAbstractListModel(parent) {}

int RoomListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(rooms_.size());
}

int RoomListModel::joinedCount() const {
    int count = 0;
    for (const auto& r : rooms_) {
        if (!r.isInvite) count++;
    }
    return count;
}

QVariant RoomListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= (int)rooms_.size())
        return {};
    const auto& r = rooms_[index.row()];

    switch (role) {
        case NameRole:         return QString::fromStdString(r.name);
        case LastMessageRole:  return QString::fromStdString(r.lastMessage);
        case RoomIdRole:       return QString::fromStdString(r.roomId);
        case LastSenderRole:   return QString::fromStdString(r.lastSender);
        case LastActivityRole: return static_cast<qulonglong>(r.lastActivityTs);
        case UnreadRole:       return r.unreadCount;
        case IsDirectRole:     return r.isDirect;
        case IsEncryptedRole:  return r.isEncrypted;
        case IsSpaceRole:      return r.isSpace;
        case AvatarUrlRole:    return QString::fromStdString(r.avatarUrl);
        case IsInviteRole:     return r.isInvite;
        case InviterRole:      return QString::fromStdString(r.inviterId);
        case TypingUsersRole: {
            QStringList names;
            for (const auto& u : r.typingUsers) {
                names << QString::fromStdString(u);
            }
            return names;
        }
        case Qt::ToolTipRole:
            return QString("%1\n%2\nunread: %3")
                .arg(QString::fromStdString(r.name))
                .arg(QString::fromStdString(r.lastMessage))
                .arg(r.unreadCount);
    }
    return {};
}

bool RoomListModel::upsertRoom(const RoomData& room) {
    int row = findRowByRoomId(room.roomId);
    if (row >= 0) {
        // Update existing
        bool changed = false;
        auto& existing = rooms_[row];
        if (existing.name != room.name && !room.name.empty()) {
            existing.name = room.name;
            changed = true;
        }
        if (existing.lastActivityTs != room.lastActivityTs) {
            existing.lastActivityTs = room.lastActivityTs;
            existing.lastMessage = room.lastMessage;
            existing.lastSender = room.lastSender;
            changed = true;
        }
        if (existing.unreadCount != room.unreadCount) {
            existing.unreadCount = room.unreadCount;
            changed = true;
        }
        if (existing.avatarUrl != room.avatarUrl && !room.avatarUrl.empty()) {
            existing.avatarUrl = room.avatarUrl;
            changed = true;
        }
        if (existing.isEncrypted != room.isEncrypted) {
            existing.isEncrypted = room.isEncrypted;
            changed = true;
        }
        if (existing.isInvite != room.isInvite) {
            existing.isInvite = room.isInvite;
            changed = true;
        }
        if (existing.inviterId != room.inviterId && !room.inviterId.empty()) {
            existing.inviterId = room.inviterId;
            changed = true;
        }
        if (changed) {
            emit dataChanged(index(row), index(row));
        }
        return changed;
    }

    // Insert new — sorted by lastActivity descending
    auto it = std::upper_bound(rooms_.begin(), rooms_.end(), room,
        [](const RoomData& a, const RoomData& b) {
            return a.lastActivityTs > b.lastActivityTs;
        });
    int newRow = static_cast<int>(std::distance(rooms_.begin(), it));
    beginInsertRows(QModelIndex(), newRow, newRow);
    rooms_.insert(it, room);
    // Rebuild index from newRow onwards (insertion shifted later rows)
    for (int i = newRow; i < (int)rooms_.size(); ++i) {
        index_[rooms_[i].roomId] = i;
    }
    endInsertRows();
    return true;
}

void RoomListModel::clear() {
    if (rooms_.empty()) return;
    beginResetModel();
    rooms_.clear();
    index_.clear();
    endResetModel();
}

bool RoomListModel::removeRoom(const std::string& roomId) {
    int row = findRowByRoomId(roomId);
    if (row < 0) return false;
    beginRemoveRows(QModelIndex(), row, row);
    rooms_.erase(rooms_.begin() + row);
    index_.erase(roomId);
    // Rebuild index for rows after the removed one
    for (int i = row; i < (int)rooms_.size(); ++i) {
        index_[rooms_[i].roomId] = i;
    }
    endRemoveRows();
    return true;
}

const RoomData* RoomListModel::at(int row) const {
    if (row < 0 || row >= (int)rooms_.size()) return nullptr;
    return &rooms_[row];
}

int RoomListModel::findRowByRoomId(const std::string& roomId) const {
    auto it = index_.find(roomId);
    if (it == index_.end()) return -1;
    // Validate index still in sync (defensive)
    if (it->second < 0 || it->second >= (int)rooms_.size() ||
        rooms_[it->second].roomId != roomId) {
        return -1;
    }
    return it->second;
}

void RoomListModel::updateHeader(QLabel* header, QLabel* inviteHeader) const {
    int inviteCount = 0;
    int joinedCount = 0;
    for (const auto& r : rooms_) {
        if (r.isInvite) inviteCount++;
        else joinedCount++;
    }
    header->setText(QString(" Chats (%1) ").arg(joinedCount));
    if (inviteCount > 0) {
        inviteHeader->setText(QString("  Invitations (%1) ").arg(inviteCount));
        inviteHeader->show();
    } else {
        inviteHeader->hide();
    }
}

} // namespace progressive::desktop
