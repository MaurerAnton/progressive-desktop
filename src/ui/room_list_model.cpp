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
    endInsertRows();
    return true;
}

void RoomListModel::clear() {
    if (rooms_.empty()) return;
    beginResetModel();
    rooms_.clear();
    endResetModel();
}

const RoomData* RoomListModel::at(int row) const {
    if (row < 0 || row >= (int)rooms_.size()) return nullptr;
    return &rooms_[row];
}

int RoomListModel::findRowByRoomId(const std::string& roomId) const {
    for (size_t i = 0; i < rooms_.size(); ++i) {
        if (rooms_[i].roomId == roomId) return static_cast<int>(i);
    }
    return -1;
}

} // namespace progressive::desktop
