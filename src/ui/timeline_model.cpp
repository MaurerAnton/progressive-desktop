#include "timeline_model.hpp"

#include <QVariantList>
#include <QMetaType>

Q_DECLARE_METATYPE(progressive::desktop::ReactionData)

namespace progressive::desktop {

TimelineModel::TimelineModel(QObject* parent) : QAbstractListModel(parent) {}

int TimelineModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(events_.size());
}

QVariant TimelineModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= (int)events_.size())
        return {};
    const auto& e = events_[index.row()];
    switch (role) {
        case SenderRole:       return QString::fromStdString(e.senderId);
        case SenderNameRole:   return QString::fromStdString(e.senderName);
        case TimeRole:         return static_cast<qulonglong>(e.originServerTs);
        case TypeRole:         return QString::fromStdString(e.type);
        case MsgTypeRole:      return QString::fromStdString(e.msgtype);
        case BodyRole:         return QString::fromStdString(e.body);
        case ContentJsonRole:  return QString::fromStdString(e.contentJson);
        case MxcUrlRole:       return QString::fromStdString(e.mxcUrl);
        case MimetypeRole:     return QString::fromStdString(e.mimetype);
        case IsReplyRole:      return e.isReply;
        case ReplyToRole:      return QString::fromStdString(e.replyToEventId);
        case IsThreadRootRole: return e.isThreadRoot;
        case ThreadCountRole:  return e.threadReplyCount;
        case IsThreadReplyRole: return e.isThreadReply;
        case ThreadRootIdRole: return QString::fromStdString(e.threadRootId);
        case IsPinnedRole:     return e.isPinned;
        case ImageRole:        return e.image;
        case ImageLoadedRole:  return e.imageLoaded;
        case IsMovieRole:      return e.isMovie;
        case EventIdRole:      return QString::fromStdString(e.eventId);
        case AvatarUrlRole:    return QString::fromStdString(e.avatarUrl);
        case ReactionsRole: {
            // Convert reactions to a QStringList of "emoji (count)" entries
            // for easy rendering in the delegate.
            QStringList list;
            for (const auto& r : e.reactions) {
                list << QString::fromStdString(r.emoji) + " " + QString::number(r.count);
            }
            return list;
        }
    }
    return {};
}

void TimelineModel::appendBack(const DisplayedEvent& evt) {
    if (!evt.eventId.empty() && seenIds_.count(evt.eventId)) return;
    if (!evt.eventId.empty()) seenIds_.insert(evt.eventId);

    int row = static_cast<int>(events_.size());
    beginInsertRows(QModelIndex(), row, row);
    events_.push_back(evt);
    endInsertRows();
}

void TimelineModel::replaceEcho(const std::string& tempEventId, const DisplayedEvent& realEvent) {
    // Find the echo by temp event ID
    for (size_t i = 0; i < events_.size(); ++i) {
        if (events_[i].eventId == tempEventId) {
            // Replace the echo with the real event
            if (!realEvent.eventId.empty() && seenIds_.count(realEvent.eventId)) {
                // Real event already exists — just remove the echo
                beginRemoveRows(QModelIndex(), i, i);
                events_.erase(events_.begin() + i);
                endRemoveRows();
            } else {
                events_[i] = realEvent;
                if (!realEvent.eventId.empty()) seenIds_.insert(realEvent.eventId);
                emit dataChanged(index(i), index(i));
            }
            return;
        }
    }
    // Echo not found — just append the real event
    appendBack(realEvent);
}

void TimelineModel::markDeleted(const std::string& eventId) {
    int row = findRow(eventId);
    if (row < 0) return;
    events_[row].body = "[Message deleted]";
    events_[row].msgtype = "m.notice";
    events_[row].mxcUrl.clear();
    emit dataChanged(index(row), index(row), {BodyRole, MsgTypeRole, MxcUrlRole});
}

void TimelineModel::updateBody(const std::string& eventId, const std::string& newBody) {
    int row = findRow(eventId);
    if (row < 0) return;
    events_[row].body = newBody;
    events_[row].msgtype = "m.text";  // edited messages are always m.text
    emit dataChanged(index(row), index(row), {BodyRole, MsgTypeRole});
}

void TimelineModel::appendFront(const std::vector<DisplayedEvent>& evts) {
    // Filter out duplicates
    std::vector<DisplayedEvent> newOnes;
    for (const auto& e : evts) {
        if (e.eventId.empty() || !seenIds_.count(e.eventId)) {
            if (!e.eventId.empty()) seenIds_.insert(e.eventId);
            newOnes.push_back(e);
        }
    }
    if (newOnes.empty()) return;

    int n = static_cast<int>(newOnes.size());
    beginInsertRows(QModelIndex(), 0, n - 1);
    events_.insert(events_.begin(), newOnes.rbegin(), newOnes.rend());
    endInsertRows();
}

void TimelineModel::clear() {
    if (events_.empty()) return;
    beginResetModel();
    events_.clear();
    seenIds_.clear();
    endResetModel();
}

void TimelineModel::setImage(const std::string& eventId, const QImage& img) {
    int row = findRow(eventId);
    if (row < 0) return;
    events_[row].image = img;
    events_[row].imageLoaded = true;
    emit dataChanged(index(row), index(row), {ImageRole, ImageLoadedRole});
}

void TimelineModel::addReaction(const std::string& eventId, const std::string& emoji,
                                  const std::string& userId, const std::string& reactionEventId) {
    int row = findRow(eventId);
    if (row < 0) return;
    auto& reactions = events_[row].reactions;
    for (auto& r : reactions) {
        if (r.emoji == emoji) {
            r.count++;
            r.userIds.push_back(userId);
            if (userId == /* our user */ "") {}  // can't check here — caller sets addedByMe
            if (!reactionEventId.empty()) r.reactionEventId = reactionEventId;
            emit dataChanged(index(row), index(row), {ReactionsRole});
            return;
        }
    }
    reactions.push_back({emoji, 1, false, {userId}, reactionEventId});
    emit dataChanged(index(row), index(row), {ReactionsRole});
}

void TimelineModel::removeReaction(const std::string& eventId, const std::string& emoji, const std::string& userId) {
    int row = findRow(eventId);
    if (row < 0) return;
    auto& reactions = events_[row].reactions;
    for (auto it = reactions.begin(); it != reactions.end(); ++it) {
        if (it->emoji == emoji) {
            auto& users = it->userIds;
            users.erase(std::remove(users.begin(), users.end(), userId), users.end());
            it->count = static_cast<int>(users.size());
            if (it->count <= 0) reactions.erase(it);
            emit dataChanged(index(row), index(row), {ReactionsRole});
            return;
        }
    }
}

void TimelineModel::setPinned(const std::string& eventId, bool pinned) {
    int row = findRow(eventId);
    if (row < 0) return;
    events_[row].isPinned = pinned;
    emit dataChanged(index(row), index(row), {IsPinnedRole});
}

const DisplayedEvent* TimelineModel::at(int row) const {
    if (row < 0 || row >= (int)events_.size()) return nullptr;
    return &events_[row];
}

DisplayedEvent* TimelineModel::at(int row) {
    if (row < 0 || row >= (int)events_.size()) return nullptr;
    return &events_[row];
}

int TimelineModel::findRow(const std::string& eventId) const {
    for (size_t i = 0; i < events_.size(); ++i) {
        if (events_[i].eventId == eventId) return static_cast<int>(i);
    }
    return -1;
}

} // namespace progressive::desktop
