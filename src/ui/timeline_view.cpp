// src/ui/timeline_view.cpp

#include "timeline_view.hpp"

#include <progressive/markdown.hpp>
#include <progressive/json_parser.hpp>
#include <progressive/event_models.hpp>

#include <QDateTime>
#include <QScrollBar>
#include <QTextCursor>
#include <sstream>

namespace progressive::desktop {

namespace {

// Extract a string field from a JSON content blob, handling both
// "body":"..." and "formatted_body":"..." if present.
std::string extractBody(const std::string& contentJson) {
    auto body = progressive::parseJsonStringValue(contentJson, "body");
    if (!body.empty()) return body;
    auto fb = progressive::parseJsonStringValue(contentJson, "formatted_body");
    return fb;
}

std::string extractMsgtype(const std::string& contentJson) {
    return progressive::parseJsonStringValue(contentJson, "msgtype");
}

// Format a timestamp as "HH:MM" (today) or "MMM DD HH:MM" (older).
QString formatTime(int64_t ts) {
    if (ts <= 0) return "?";
    QDateTime dt = QDateTime::fromMSecsSinceEpoch(ts);
    QDateTime now = QDateTime::currentDateTime();
    if (dt.date() == now.date()) return dt.toString("HH:mm");
    return dt.toString("MMM dd HH:mm");
}

// Strip the server part from a Matrix user ID: @user:server.org → user
std::string shortName(const std::string& mxid) {
    if (mxid.empty()) return "(unknown)";
    if (mxid[0] == '@') {
        auto colon = mxid.find(':');
        if (colon != std::string::npos) return mxid.substr(1, colon - 1);
        return mxid.substr(1);
    }
    return mxid;
}

QString htmlEscape(const std::string& s) {
    return QString::fromStdString(s).toHtmlEscaped();
}

} // namespace

TimelineView::TimelineView(QWidget* parent)
    : QTextBrowser(parent) {
    setOpenExternalLinks(true);
    setReadOnly(true);
    document()->setDocumentMargin(8);
}

void TimelineView::appendEvent(const std::string& eventId,
                                int64_t originServerTs,
                                const std::string& senderId,
                                const std::string& type,
                                const std::string& contentJson) {
    if (!eventId.empty()) {
        if (seenEventIds_.count(eventId)) return;
        seenEventIds_.insert(eventId);
    }

    std::ostringstream html;
    QString time = formatTime(originServerTs);
    QString sender = htmlEscape(shortName(senderId));

    if (type == "m.room.message") {
        std::string body = extractBody(contentJson);
        std::string msgtype = extractMsgtype(contentJson);
        QString bodyHtml;
        if (msgtype == "m.text" || msgtype.empty()) {
            // Render markdown
            std::string rendered = progressive::markdownToHtml(body);
            // If markdown produced nothing, fallback to escaped plain text
            if (rendered.empty()) {
                bodyHtml = "<span style='color:#222'>" + htmlEscape(body) + "</span>";
            } else {
                bodyHtml = QString::fromStdString(rendered);
            }
        } else if (msgtype == "m.emote") {
            bodyHtml = QString("<i>* %1 %2</i>").arg(sender, htmlEscape(body));
            sender = "&nbsp;";  // emote doesn't show sender separately
        } else if (msgtype == "m.notice") {
            bodyHtml = QString("<span style='color:#666'>%1</span>")
                          .arg(htmlEscape(body));
        } else if (msgtype == "m.image" || msgtype == "m.video" ||
                   msgtype == "m.audio" || msgtype == "m.file") {
            bodyHtml = QString("<i>[%1 attachment]</i>").arg(QString::fromStdString(msgtype).mid(2));
        } else {
            bodyHtml = htmlEscape(body);
        }

        html << "<p style='margin:4px 0'><b>" << sender.toStdString()
             << "</b> <span style='color:#999;font-size:smaller'>"
             << time.toStdString() << "</span><br>"
             << bodyHtml.toStdString() << "</p>";
    } else if (type == "m.room.encrypted") {
        // Phase 2: no decryption yet
        html << "<p style='margin:4px 0;color:#999'><b>" << sender.toStdString()
             << "</b> <span style='color:#999;font-size:smaller'>"
             << time.toStdString() << "</span><br>"
             << "<i>[encrypted message — decryption in Phase 4]</i></p>";
    } else if (type == "m.room.member") {
        // Member events — render as a one-liner
        std::string membership = progressive::parseJsonStringValue(contentJson, "membership");
        std::string displayname = progressive::parseJsonStringValue(contentJson, "displayname");
        std::string who = displayname.empty() ? shortName(senderId) : displayname;
        std::string msg;
        if (membership == "join")       msg = who + " joined";
        else if (membership == "leave") msg = who + " left";
        else if (membership == "invite")msg = who + " was invited";
        else if (membership == "ban")   msg = who + " was banned";
        else                            msg = who + " " + membership;
        html << "<p style='margin:4px 0;color:#888;font-size:smaller'>"
             << time.toStdString() << " — " << msg << "</p>";
    } else if (type == "m.room.redaction") {
        html << "<p style='margin:4px 0;color:#888;font-size:smaller'>"
             << time.toStdString() << " — " << sender.toStdString()
             << " redacted a message</p>";
    } else {
        // Other state events — minimal display
        std::string eventType = type.empty() ? "event" : type;
        if (eventType.substr(0, 7) == "m.room.") eventType = eventType.substr(7);
        html << "<p style='margin:4px 0;color:#888;font-size:smaller'>"
             << time.toStdString() << " — " << sender.toStdString()
             << " " << eventType << "</p>";
    }

    appendHtml(QString::fromStdString(html.str()));
    // Auto-scroll to bottom
    QScrollBar* bar = verticalScrollBar();
    bar->setValue(bar->maximum());
}

void TimelineView::appendLocalEcho(const std::string& body) {
    std::ostringstream html;
    html << "<p style='margin:4px 0;opacity:0.7'><b>you</b>"
         << "<span style='color:#999;font-size:smaller'> (sending)</span><br>"
         << htmlEscape(body).toStdString() << "</p>";
    appendHtml(QString::fromStdString(html.str()));
    QScrollBar* bar = verticalScrollBar();
    bar->setValue(bar->maximum());
}

void TimelineView::clear() {
    QTextBrowser::clear();
    seenEventIds_.clear();
}

} // namespace progressive::desktop
