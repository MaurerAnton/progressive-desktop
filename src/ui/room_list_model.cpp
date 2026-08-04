// src/ui/room_list_model.cpp

#include "room_list_model.hpp"

#include <QFontMetrics>
#include <algorithm>

namespace progressive::desktop {

RoomListModel::RoomListModel(QObject* parent)
    : QAbstractListModel(parent) {}

int RoomListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return state_.size();
}

int RoomListModel::joinedCount() const {
    return state_.joinedCount();
}

QVariant RoomListModel::data(const QModelIndex& index, int role) const {
    const RoomData* rp = state_.at(index.row());
    if (!index.isValid() || !rp) return {};
    const auto& r = *rp;

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
    auto result = state_.upsertRoom(room);
    int row = state_.upsertRow();
    switch (result) {
        case RoomState::UpsertResult::Updated:
            emit dataChanged(index(row), index(row));
            return true;
        case RoomState::UpsertResult::Inserted:
            beginInsertRows(QModelIndex(), row, row);
            endInsertRows();
            return true;
        default:
            return false;
    }
}

void RoomListModel::clear() {
    if (state_.empty()) return;
    beginResetModel();
    state_.clear();
    endResetModel();
}

bool RoomListModel::removeRoom(const std::string& roomId) {
    if (!state_.removeRoom(roomId)) return false;
    int row = state_.removeRow();
    beginRemoveRows(QModelIndex(), row, row);
    endRemoveRows();
    return true;
}

const RoomData* RoomListModel::at(int row) const {
    return state_.at(row);
}

int RoomListModel::findRowByRoomId(const std::string& roomId) const {
    return state_.findRowByRoomId(roomId);
}

void RoomListModel::updateHeader(QLabel* header, QLabel* inviteHeader) const {
    int inviteCount = 0;
    int joinedCount = 0;
    for (int i = 0; i < state_.size(); ++i) {
        const auto* r = state_.at(i);
        if (!r) continue;
        if (r->isInvite) inviteCount++;
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
