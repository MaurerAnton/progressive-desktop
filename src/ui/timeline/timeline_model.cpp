#include "timeline_model.hpp"
#include "../shared/image_loader.hpp"

#include <QVariantList>
#include <QMetaType>
#include <QScrollBar>
#include <QListView>

Q_DECLARE_METATYPE(progressive::desktop::ReactionData)

namespace progressive::desktop {

static std::string normEmoji(const std::string& e) {
    std::string out;
    for (size_t i = 0; i < e.size(); ++i) {
        unsigned char c = e[i];
        if (c == 0xEF && i + 2 < e.size() && e[i+1] == 0xB8 && e[i+2] == 0x8F) {
            i += 2; continue;
        }
        if (c == 0xE2 && i + 2 < e.size() && e[i+1] == 0x80 && e[i+2] == 0x8D) {
            i += 2; continue;
        }
        out += c;
    }
    return out;
}

TimelineModel::TimelineModel(QObject* parent) : QAbstractListModel(parent) {}

void TimelineModel::setView(QListView* view) { view_ = view; }

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
        case ImageRole:
            return loader_ ? loader_->getCached(e.mxcUrl) : QImage();
        case ImageLoadedRole:
            return loader_ ? loader_->hasImage(e.mxcUrl) : false;
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

static const int MAX_TIMELINE_EVENTS = 200;
static constexpr int64_t kMergeWindowMs = 300000;  // 5 minutes same-sender merge window

// Update groupFirst/groupLast on all events after model mutation
static void updateGroupMarkers(std::vector<DisplayedEvent>& events) {
    for (size_t i = 0; i < events.size(); ++i) {
        auto& e = events[i];
        bool isSystem = (e.type == "m.room.member" || e.type == "m.room.redaction");
        bool isEmote = (e.msgtype == "m.emote");
        if (isSystem || isEmote) { e.groupFirst = true; e.groupLast = true; continue; }
        bool first = true;
        for (int p = (int)i - 1; p >= 0; --p) {
            auto& prev = events[p];
            if (prev.type != "m.room.member" && prev.type != "m.room.redaction" && prev.msgtype != "m.emote") {
                int64_t gap = e.originServerTs - prev.originServerTs;
                first = (prev.senderId != e.senderId || gap > kMergeWindowMs || gap < -kMergeWindowMs);
                break;
            }
        }
        bool last = true;
        for (size_t n = i + 1; n < events.size(); ++n) {
            auto& next = events[n];
            if (next.type != "m.room.member" && next.type != "m.room.redaction" && next.msgtype != "m.emote") {
                int64_t gap = next.originServerTs - e.originServerTs;
                last = (next.senderId != e.senderId || gap > kMergeWindowMs || gap < -kMergeWindowMs);
                break;
            }
        }
        e.groupFirst = first;
        e.groupLast = last;
    }
}

void TimelineModel::appendBack(const DisplayedEvent& evt) {
    if (!evt.eventId.empty() && seenIds_.count(evt.eventId)) return;
    if (!evt.eventId.empty()) seenIds_.insert(evt.eventId);

    int savedScroll = 0;
    bool wasAtBottom = true;
    if (view_ && view_->verticalScrollBar()) {
        auto* sb = view_->verticalScrollBar();
        savedScroll = sb->value();
        wasAtBottom = (savedScroll >= sb->maximum() - 10);
    }

    int row = static_cast<int>(events_.size());
    beginInsertRows(QModelIndex(), row, row);
    events_.push_back(evt);
    endInsertRows();
    if (!evt.eventId.empty()) rowIndex_[evt.eventId] = row;
    if (evt.isThreadReply && !evt.threadRootId.empty()) {
        auto it = rowIndex_.find(evt.threadRootId);
        if (it != rowIndex_.end() && it->second >= 0 && it->second < (int)events_.size()) {
            events_[it->second].threadReplyCount++;
        }
    }
    updateGroupMarkers(events_);
    if (!events_.empty()) emit dataChanged(index(0), index(static_cast<int>(events_.size()) - 1));

    if (static_cast<int>(events_.size()) > MAX_TIMELINE_EVENTS) {
        int excess = static_cast<int>(events_.size()) - MAX_TIMELINE_EVENTS;
        beginRemoveRows(QModelIndex(), 0, excess - 1);
        for (int i = 0; i < excess; ++i) {
            seenIds_.erase(events_[static_cast<size_t>(i)].eventId);
        }
        events_.erase(events_.begin(), events_.begin() + excess);
        endRemoveRows();
    }

    if (view_ && view_->verticalScrollBar() && !wasAtBottom && savedScroll > 0) {
        view_->verticalScrollBar()->setValue(savedScroll);
    }
}

void TimelineModel::replaceEcho(const std::string& tempEventId, const DisplayedEvent& realEvent) {
    auto rit = rowIndex_.find(tempEventId);
    if (rit == rowIndex_.end()) {
        appendBack(realEvent);
        return;
    }
    int i = rit->second;
    if (i < 0 || i >= (int)events_.size()) { appendBack(realEvent); return; }
    rowIndex_.erase(tempEventId);

    if (!realEvent.eventId.empty() && seenIds_.count(realEvent.eventId)) {
        beginRemoveRows(QModelIndex(), i, i);
        events_.erase(events_.begin() + i);
        endRemoveRows();
        // Rebuild index after removal
        rowIndex_.clear();
        for (size_t j = 0; j < events_.size(); ++j)
            if (!events_[j].eventId.empty()) rowIndex_[events_[j].eventId] = (int)j;
    } else {
        events_[i] = realEvent;
        if (!realEvent.eventId.empty()) {
            seenIds_.insert(realEvent.eventId);
            rowIndex_[realEvent.eventId] = i;
        }
        emit dataChanged(index(i), index(i));
    }
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

bool TimelineModel::replaceEvent(const std::string& eventId, const DisplayedEvent& newEvent) {
    int row = findRow(eventId);
    if (row < 0) return false;
    auto& e = events_[row];
    e.body = newEvent.body;
    e.msgtype = newEvent.msgtype;
    e.contentJson = newEvent.contentJson;
    e.mxcUrl = newEvent.mxcUrl;
    e.mimetype = newEvent.mimetype;
    e.isThreadRoot = newEvent.isThreadRoot;
    e.threadReplyCount = newEvent.threadReplyCount;
    e.isThreadReply = newEvent.isThreadReply;
    e.threadRootId = newEvent.threadRootId;
    e.isMovie = newEvent.isMovie;
    emit dataChanged(index(row), index(row));
    return true;
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
    rowIndex_.clear();
    for (size_t i = 0; i < events_.size(); ++i) {
        if (!events_[i].eventId.empty()) rowIndex_[events_[i].eventId] = (int)i;
    }
    for (const auto& evt : newOnes) {
        if (evt.isThreadReply && !evt.threadRootId.empty()) {
            auto it = rowIndex_.find(evt.threadRootId);
            if (it != rowIndex_.end() && it->second >= 0 && it->second < (int)events_.size()) {
                events_[it->second].threadReplyCount++;
            }
        }
    }
    updateGroupMarkers(events_);
    if (!events_.empty()) emit dataChanged(index(0), index(static_cast<int>(events_.size()) - 1));
}

void TimelineModel::appendBackBatch(const std::vector<DisplayedEvent>& events) {
    std::vector<DisplayedEvent> filtered;
    for (const auto& evt : events) {
        if (!evt.eventId.empty() && seenIds_.count(evt.eventId)) continue;
        if (!evt.eventId.empty()) seenIds_.insert(evt.eventId);
        filtered.push_back(evt);
    }
    if (filtered.empty()) return;

    int savedScroll = 0;
    bool wasAtBottom = true;
    if (view_ && view_->verticalScrollBar()) {
        auto* sb = view_->verticalScrollBar();
        savedScroll = sb->value();
        wasAtBottom = (savedScroll >= sb->maximum() - 10);
    }

    int first = static_cast<int>(events_.size());
    int last = first + static_cast<int>(filtered.size()) - 1;
    beginInsertRows(QModelIndex(), first, last);
    events_.insert(events_.end(), filtered.begin(), filtered.end());
    endInsertRows();

    for (int i = 0; i < static_cast<int>(events_.size()); ++i)
        if (!events_[i].eventId.empty()) rowIndex_[events_[i].eventId] = i;
    for (const auto& evt : filtered) {
        if (evt.isThreadReply && !evt.threadRootId.empty()) {
            auto it = rowIndex_.find(evt.threadRootId);
            if (it != rowIndex_.end() && it->second >= 0 && it->second < (int)events_.size()) {
                events_[it->second].threadReplyCount++;
            }
        }
    }
    updateGroupMarkers(events_);
    if (!events_.empty()) emit dataChanged(index(0), index(static_cast<int>(events_.size()) - 1));

    if (view_ && view_->verticalScrollBar() && !wasAtBottom && savedScroll > 0) {
        view_->verticalScrollBar()->setValue(savedScroll);
    }
}

void TimelineModel::clear() {
    if (events_.empty()) return;
    beginResetModel();
    events_.clear();
    seenIds_.clear();
    rowIndex_.clear();
    endResetModel();
}

void TimelineModel::imageLoaded(const std::string& mxcUrl) {
    // Re-layout: row HEIGHTS depend on ImageLoadedRole — a bare repaint is
    // not enough. Emit dataChanged on the image roles for every matching row.
    // The model's copy is UI-thread-only (engine thread contract) — no lock.
    for (int row = 0; row < static_cast<int>(events_.size()); ++row) {
        if (events_[row].mxcUrl != mxcUrl) continue;
        emit dataChanged(index(row), index(row), {ImageRole, ImageLoadedRole});
    }
}

void TimelineModel::addReaction(const std::string& eventId, const std::string& emoji,
                                  const std::string& userId, const std::string& reactionEventId,
                                  const std::string& myUserId) {
    int row = findRow(eventId);
    if (row < 0) return;
    auto& reactions = events_[row].reactions;
    for (auto& r : reactions) {
        if (normEmoji(r.emoji) == normEmoji(emoji)) {
            for (const auto& u : r.userIds) {
                if (u == userId) return;
            }
            r.count++;
            r.userIds.push_back(userId);
            if (!myUserId.empty() && userId == myUserId) r.addedByMe = true;
            if (!reactionEventId.empty()) r.reactionEventId = reactionEventId;
            emit dataChanged(index(row), index(row));
            return;
        }
    }
    reactions.push_back({emoji, 1, false, {userId}, reactionEventId});
    if (!myUserId.empty() && userId == myUserId) reactions.back().addedByMe = true;
    emit dataChanged(index(row), index(row));
}

void TimelineModel::removeReaction(const std::string& eventId, const std::string& emoji, const std::string& userId) {
    int row = findRow(eventId);
    if (row < 0) return;
    auto& reactions = events_[row].reactions;
    for (auto it = reactions.begin(); it != reactions.end(); ++it) {
        if (normEmoji(it->emoji) == normEmoji(emoji)) {
            auto& users = it->userIds;
            users.erase(std::remove(users.begin(), users.end(), userId), users.end());
            it->count = static_cast<int>(users.size());
            if (it->count <= 0) reactions.erase(it);
            emit dataChanged(index(row), index(row));
            return;
        }
    }
}

std::string TimelineModel::myReactionId(const std::string& eventId,
                                          const std::string& emoji,
                                          const std::string& myUserId) const {
    int row = findRow(eventId);
    if (row < 0) return {};
    for (const auto& r : events_[row].reactions) {
        if (normEmoji(r.emoji) == normEmoji(emoji)) {
            for (const auto& u : r.userIds) {
                if (u == myUserId) return r.reactionEventId;
            }
        }
    }
    return {};
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
    auto it = rowIndex_.find(eventId);
    return it != rowIndex_.end() ? it->second : -1;
}

} // namespace progressive::desktop
