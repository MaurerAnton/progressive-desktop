// src/ui/timeline_model.hpp — QAbstractListModel for chat timeline.
//
// Replaces QTextBrowser. Supports text, images, GIFs, reactions,
// thread indicators, pinned messages, member events.
#pragma once
#include <QAbstractListModel>
#include <QImage>
#include <QMovie>
#include <vector>
#include <string>
#include <unordered_set>
#include <unordered_map>

namespace progressive::desktop {

struct ReactionData {
    std::string emoji;
    int count = 0;
    bool addedByMe = false;
    std::vector<std::string> userIds;
};

struct DisplayedEvent {
    std::string eventId;
    std::string senderId;
    std::string senderName;        // displayname or localpart
    std::string type;              // m.room.message, m.room.member, etc.
    std::string msgtype;           // m.text, m.image, m.emote, etc.
    std::string body;              // text body (for text messages)
    std::string contentJson;       // raw content JSON (for images, etc.)
    std::string mxcUrl;            // for images: mxc:// URL
    std::string mimetype;          // for images: image/gif, image/png, etc.
    int64_t originServerTs = 0;
    bool isReply = false;          // has m.relates_to m.reply
    std::string replyToEventId;    // if isReply
    bool isThreadRoot = false;     // has m.thread replies
    int threadReplyCount = 0;
    bool isPinned = false;
    std::vector<ReactionData> reactions;
    QImage image;                  // cached thumbnail (empty if not loaded yet)
    bool imageLoaded = false;
    bool isMovie = false;          // true for animated GIF
};

class TimelineModel : public QAbstractListModel {
    Q_OBJECT
public:
    explicit TimelineModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    enum Roles {
        SenderRole = Qt::UserRole + 1,
        SenderNameRole,
        TimeRole,
        TypeRole,
        MsgTypeRole,
        BodyRole,
        ContentJsonRole,
        MxcUrlRole,
        MimetypeRole,
        IsReplyRole,
        ReplyToRole,
        IsThreadRootRole,
        ThreadCountRole,
        IsPinnedRole,
        ReactionsRole,
        ImageRole,
        ImageLoadedRole,
        IsMovieRole,
        EventIdRole,
    };

    // Add events (from sync or pagination). Deduplicates by event_id.
    void appendBack(const DisplayedEvent& evt);
    void appendFront(const std::vector<DisplayedEvent>& evts);  // for pagination
    void clear();

    // Update image for a specific event (when async load completes).
    void setImage(const std::string& eventId, const QImage& img);

    // Add/update a reaction on an event.
    void addReaction(const std::string& eventId, const std::string& emoji, const std::string& userId);
    void removeReaction(const std::string& eventId, const std::string& emoji, const std::string& userId);

    // Mark an event as pinned/unpinned.
    void setPinned(const std::string& eventId, bool pinned);

    // Get event by row index.
    const DisplayedEvent* at(int row) const;
    DisplayedEvent* at(int row);

    // Find row by event_id. Returns -1 if not found.
    int findRow(const std::string& eventId) const;

private:
    std::vector<DisplayedEvent> events_;
    std::unordered_set<std::string> seenIds_;
};

} // namespace progressive::desktop
