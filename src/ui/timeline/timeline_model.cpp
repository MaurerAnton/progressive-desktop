#include "timeline_model.hpp"
#include "../shared/image_loader.hpp"

#include <QVariantList>
#include <QMetaType>
#include <QScrollBar>
#include <QListView>

Q_DECLARE_METATYPE(progressive::desktop::ReactionData)

namespace progressive::desktop {



TimelineModel::TimelineModel(QObject* parent) : QAbstractListModel(parent) {}

void TimelineModel::setView(QListView* view) { view_ = view; }

int TimelineModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(state_.size());
}

QVariant TimelineModel::data(const QModelIndex& index, int role) const {
    const auto* eptr = state_.at(index.row());
    if (!index.isValid() || !eptr) return {};
    const auto& e = *eptr;
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
// Update groupFirst/groupLast on all events after model mutation
void TimelineModel::appendBack(const DisplayedEvent& evt) {
    int savedScroll = 0;
    bool wasAtBottom = true;
    if (view_ && view_->verticalScrollBar()) {
        auto* sb = view_->verticalScrollBar();
        savedScroll = sb->value();
        wasAtBottom = (savedScroll >= sb->maximum() - 10);
    }

    auto r = state_.appendBack(evt);
    if (!r.changed) return;
    int row = r.insertedRow;
    beginInsertRows(QModelIndex(), row, row);
    endInsertRows();
    if (r.threadRootRow >= 0)
        emit dataChanged(index(r.threadRootRow), index(r.threadRootRow), {ThreadCountRole});
    if (!state_.empty())
        emit dataChanged(index(0), index(static_cast<int>(state_.size()) - 1));
    if (r.evictedCount > 0) {
        beginRemoveRows(QModelIndex(), 0, r.evictedCount - 1);
        endRemoveRows();
    }

    if (view_ && view_->verticalScrollBar() && !wasAtBottom && savedScroll > 0) {
        view_->verticalScrollBar()->setValue(savedScroll);
    }
}

void TimelineModel::replaceEcho(const std::string& tempEventId, const DisplayedEvent& realEvent) {
    switch (state_.replaceEcho(tempEventId, realEvent)) {
        case TimelineState::EchoResult::Appended: {
            int row = static_cast<int>(state_.size()) - 1;
            beginInsertRows(QModelIndex(), row, row);
            endInsertRows();
            emit dataChanged(index(0), index(row));
            break;
        }
        case TimelineState::EchoResult::RemovedDuplicate: {
            int i = state_.findRow(realEvent.eventId);
            if (i < 0) i = state_.findRow(tempEventId);
            if (i >= 0) { beginRemoveRows(QModelIndex(), i, i); endRemoveRows(); }
            break;
        }
        case TimelineState::EchoResult::Replaced: {
            int i = state_.findRow(tempEventId);
            if (i < 0) i = state_.findRow(realEvent.eventId);
            if (i >= 0) emit dataChanged(index(i), index(i));
            break;
        }
        default: break;
    }
}

void TimelineModel::markDeleted(const std::string& eventId) {
    int row = state_.findRow(eventId);
    if (!state_.markDeleted(eventId)) return;
    emit dataChanged(index(row), index(row), {BodyRole, MsgTypeRole, MxcUrlRole});
}

void TimelineModel::updateBody(const std::string& eventId, const std::string& newBody) {
    int row = state_.findRow(eventId);
    if (!state_.updateBody(eventId, newBody)) return;
    emit dataChanged(index(row), index(row), {BodyRole, MsgTypeRole});
}

bool TimelineModel::replaceEvent(const std::string& eventId, const DisplayedEvent& newEvent) {
    int row = state_.findRow(eventId);
    if (!state_.replaceEvent(eventId, newEvent)) return false;
    emit dataChanged(index(row), index(row));
    return true;
}

void TimelineModel::appendFront(const std::vector<DisplayedEvent>& evts) {
    int n = state_.appendFront(evts);
    if (n <= 0) return;
    beginInsertRows(QModelIndex(), 0, n - 1);
    endInsertRows();
    if (!state_.empty())
        emit dataChanged(index(0), index(static_cast<int>(state_.size()) - 1));
}

void TimelineModel::appendBackBatch(const std::vector<DisplayedEvent>& events) {
    int savedScroll = 0;
    bool wasAtBottom = true;
    if (view_ && view_->verticalScrollBar()) {
        auto* sb = view_->verticalScrollBar();
        savedScroll = sb->value();
        wasAtBottom = (savedScroll >= sb->maximum() - 10);
    }

    auto r = state_.appendBackBatch(events);
    if (!r.changed) return;
    beginInsertRows(QModelIndex(), r.firstRow, r.lastRow);
    endInsertRows();
    if (r.threadRootRow >= 0)
        emit dataChanged(index(r.threadRootRow), index(r.threadRootRow), {ThreadCountRole});
    if (!state_.empty())
        emit dataChanged(index(0), index(static_cast<int>(state_.size()) - 1));

    if (view_ && view_->verticalScrollBar() && !wasAtBottom && savedScroll > 0) {
        view_->verticalScrollBar()->setValue(savedScroll);
    }
}

void TimelineModel::clear() {
    if (state_.empty()) return;
    beginResetModel();
    state_.clear();
    endResetModel();
}

void TimelineModel::imageLoaded(const std::string& mxcUrl) {
    // Re-layout: row HEIGHTS depend on ImageLoadedRole — a bare repaint is
    // not enough. Emit dataChanged on the image roles for every matching row.
    // The model's copy is UI-thread-only (engine thread contract) — no lock.
    for (int row = 0; row < static_cast<int>(state_.size()); ++row) {
        const auto* e = state_.at(row);
        if (!e || e->mxcUrl != mxcUrl) continue;
        emit dataChanged(index(row), index(row), {ImageRole, ImageLoadedRole});
    }
}

void TimelineModel::addReaction(const std::string& eventId, const std::string& emoji,
                                  const std::string& userId, const std::string& reactionEventId,
                                  const std::string& myUserId) {
    int row = state_.findRow(eventId);
    if (!state_.addReaction(eventId, emoji, userId, reactionEventId)) return;
    auto* e = state_.at(row);
    if (e && !myUserId.empty() && userId == myUserId) e->reactions.back().addedByMe = true;
    emit dataChanged(index(row), index(row));
}

void TimelineModel::removeReaction(const std::string& eventId, const std::string& emoji, const std::string& userId) {
    int row = state_.findRow(eventId);
    if (!state_.removeReaction(eventId, emoji, userId)) return;
    emit dataChanged(index(row), index(row));
}

std::string TimelineModel::myReactionId(const std::string& eventId,
                                          const std::string& emoji,
                                          const std::string& myUserId) const {
    return state_.myReactionId(eventId, emoji, myUserId);
}

void TimelineModel::setPinned(const std::string& eventId, bool pinned) {
    int row = state_.findRow(eventId);
    if (!state_.setPinned(eventId, pinned)) return;
    emit dataChanged(index(row), index(row), {IsPinnedRole});
}

const DisplayedEvent* TimelineModel::at(int row) const {
    return state_.at(row);
}

DisplayedEvent* TimelineModel::at(int row) {
    return state_.at(row);
}

int TimelineModel::findRow(const std::string& eventId) const {
    return state_.findRow(eventId);
}

} // namespace progressive::desktop
