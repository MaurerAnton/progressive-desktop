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
    std::string reactionEventId;  // event_id of the m.reaction event (for redaction)
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
    bool isThreadReply = false;    // this message is a reply in a thread
    std::string threadRootId;      // root event_id if isThreadReply
    bool isPinned = false;
    std::vector<ReactionData> reactions;
    QImage image;                  // cached thumbnail (empty if not loaded yet)
    bool imageLoaded = false;
    bool isMovie = false;          // true for animated GIF
    std::string avatarUrl;         // sender's avatar mxc URL (from m.room.member)
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
        IsThreadReplyRole,
        ThreadRootIdRole,
        IsPinnedRole,
        ReactionsRole,
        ImageRole,
        ImageLoadedRole,
        IsMovieRole,
        EventIdRole,
        AvatarUrlRole,
    };

    // Add events (from sync or pagination). Deduplicates by event_id.
    void appendBack(const DisplayedEvent& evt);
    void appendFront(const std::vector<DisplayedEvent>& evts);  // for pagination
    void clear();

    // Replace a pending local echo (matched by eventId) with the real event
    // from the server. If not found, appends the real event.
    void replaceEcho(const std::string& tempEventId, const DisplayedEvent& realEvent);

    // Mark an event as deleted (redacted). Updates the body to "[Message deleted]".
    void markDeleted(const std::string& eventId);

    // Update the body of an event (for edits via m.replace).
    void updateBody(const std::string& eventId, const std::string& newBody);

    // Update image for a specific event (when async load completes).
    void setImage(const std::string& eventId, const QImage& img);

    // Add/update a reaction on an event.
    void addReaction(const std::string& eventId, const std::string& emoji,
                      const std::string& userId, const std::string& reactionEventId = "");
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
