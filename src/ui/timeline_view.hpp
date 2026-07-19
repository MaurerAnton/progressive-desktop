// src/ui/timeline_view.hpp — QTextBrowser-based chat timeline.
//
// Renders messages as HTML bubbles using progressive::markdownToHtml.
// Supports: incoming text messages, message edits, redactions (strikethrough),
// member events (joins/leaves as "X joined" lines), encrypted events
// (placeholder "decryption not yet implemented").
//
// Phase 2 limitation: E2EE decryption is NOT implemented yet — encrypted
// rooms will show "[encrypted message — decryption in Phase 4]".

#pragma once

#include <QTextBrowser>
#include <string>
#include <vector>
#include <unordered_set>

namespace progressive::desktop {

class TimelineView : public QTextBrowser {
    Q_OBJECT

public:
    explicit TimelineView(QWidget* parent = nullptr);

    // Append a single event from the timeline. Deduplicates by event_id.
    // If eventId is empty (e.g. local echo), always append.
    void appendEvent(const std::string& eventId,
                     int64_t originServerTs,
                     const std::string& senderId,
                     const std::string& type,
                     const std::string& contentJson);

    // Append a local echo (sent message pending confirmation).
    void appendLocalEcho(const std::string& body);

    // Append raw HTML (for slash command output etc.)
    void appendHtml(const QString& html) { append(html); }

    // Clear the timeline.
    void clear();

    void setRoomId(const std::string& roomId) { roomId_ = roomId; clear(); }
    const std::string& roomId() const { return roomId_; }

signals:
    void requestPagination();

private:
    std::string roomId_;
    std::unordered_set<std::string> seenEventIds_;
};

} // namespace progressive::desktop
