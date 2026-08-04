// src/ui/timeline_model.hpp — QAbstractListModel for chat timeline.
//
// Replaces QTextBrowser. Supports text, images, GIFs, reactions,
// thread indicators, pinned messages, member events.
#pragma once
#include <QAbstractListModel>
#include "core/engine/engine_types.hpp"
#include "core/engine/timeline_state.hpp"
#include <QImage>
#include <QMovie>
#include <QPointer>
#include <vector>
#include <string>
#include <unordered_set>
#include <unordered_map>

class QListView;

namespace progressive::desktop {

class ImageLoader;

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
    void appendBackBatch(const std::vector<DisplayedEvent>& events);  // single beginInsert
    void clear();

    // Replace a pending local echo (matched by eventId) with the real event
    // from the server. If not found, appends the real event.
    void replaceEcho(const std::string& tempEventId, const DisplayedEvent& realEvent);

    // Replace an existing event by eventId (e.g. [encrypted] → decrypted).
    // Mutates fields in-place + emits dataChanged. Returns false if not found.
    bool replaceEvent(const std::string& eventId, const DisplayedEvent& newEvent);

    // Mark an event as deleted (redacted). Updates the body to "[Message deleted]".
    void markDeleted(const std::string& eventId);

    // Update the body of an event (for edits via m.replace).
    void updateBody(const std::string& eventId, const std::string& newBody);

    // Update image for a specific event (when async load completes).
    void setImageLoader(ImageLoader* l) { loader_ = l; }
    // Image finished loading in the loader cache: emit dataChanged on every
    // row whose event carries this mxcUrl (re-layout — heights depend on it).
    void imageLoaded(const std::string& mxcUrl);

    // Add/update a reaction on an event.
    void addReaction(const std::string& eventId, const std::string& emoji,
                      const std::string& userId, const std::string& reactionEventId = "",
                      const std::string& myUserId = "");
    void removeReaction(const std::string& eventId, const std::string& emoji, const std::string& userId);

    std::string myReactionId(const std::string& eventId,
                               const std::string& emoji,
                               const std::string& myUserId) const;

    // Mark an event as pinned/unpinned.
    void setPinned(const std::string& eventId, bool pinned);

    // Get event by row index.
    const DisplayedEvent* at(int row) const;
    DisplayedEvent* at(int row);

    // Find row by event_id. Returns -1 if not found.
    int findRow(const std::string& eventId) const;

    void setView(QListView* view);

private:
    // The UI-thread copy of the engine's timeline (the pinned X1 contract:
    // the model NEVER reads the engine's live vector — data() serves this copy).
    TimelineState state_;
    ImageLoader* loader_ = nullptr;  // owned by MainWindow; parent-scoped connects
    QPointer<QListView> view_;
};

} // namespace progressive::desktop
