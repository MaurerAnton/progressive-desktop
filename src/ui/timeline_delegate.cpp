// src/ui/timeline_delegate.cpp — chat bubbles with avatars.
#include "timeline_delegate.hpp"
#include "timeline_model.hpp"
#include "image_loader.hpp"
#include "theme.hpp"
#include "image_loader.hpp"
#include "theme.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QTextDocument>
#include <QAbstractTextDocumentLayout>
#include <QDateTime>
#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QUrl>
#include <QRegularExpression>
#include <QPalette>
#include <QGuiApplication>
#include <QApplication>
#include <QPointer>

#include <progressive/markdown.hpp>
#include <simdjson.h>

namespace progressive::desktop {

namespace {

static const int AVATAR      = Design::avatarSize;
static const int MARGIN      = Design::margin;
static const int GAP         = Design::gap;
static const int PAD         = Design::bubblePadding;
static const int PAD_TOP     = 6;
static const int PAD_BOTTOM  = 4;
static const int RADIUS      = Design::bubbleRadius;
static const int MAX_BUBBLE_W = 480;
static const int SAME_SENDER_GAP = 2;
static const int TIME_ROW_H  = 14;

QString formatTime(int64_t ts) {
    if (ts <= 0) return "";
    QDateTime dt = QDateTime::fromMSecsSinceEpoch(ts);
    QDateTime now = QDateTime::currentDateTime();
    if (dt.date() == now.date()) return dt.toString("HH:mm");
    return dt.toString("MMM dd HH:mm");
}

QString shortName(const QString& mxid) {
    if (mxid.isEmpty()) return "?";
    if (mxid[0] == '@') {
        auto colon = mxid.indexOf(':');
        if (colon > 1) return mxid.mid(1, colon - 1);
        return mxid.mid(1);
    }
    return mxid;
}

QColor colorFromId(const QString& id) {
    uint hash = 0;
    for (QChar c : id) hash = hash * 31 + c.unicode();
    int hue = static_cast<int>(hash % 360);
    return QColor::fromHsl(hue, 180, 140);
}

// Compute rendered text height for a body + msgtype at given width.
// Returns the document height (not including extra padding).
static int calcTextHeight(const QString& body, const QString& msgtype, int textWidth) {
    if (body.isEmpty()) return 0;
    QTextDocument doc;
    doc.setDefaultFont(QFont(QApplication::font().family(), 10));
    if (msgtype == "m.text" || msgtype == "m.emote" || msgtype.isEmpty()) {
        std::string html = progressive::markdownToHtml(body.toStdString());
        if (html.empty()) doc.setPlainText(body);
        else doc.setHtml(QString::fromStdString(html));
    } else {
        doc.setPlainText(body);
    }
    doc.setTextWidth(textWidth);
    return static_cast<int>(doc.size().height());
}

static void drawRichText(QPainter* p, const QRect& r, const QString& body,
                         const QString& msgtype) {
    QTextDocument doc;
    doc.setDefaultFont(QFont(QApplication::font().family(), 10));
    if (msgtype == "m.notice" && body == "[Message deleted]") {
        QString html = "<s style='color:#666;'>" + body.toHtmlEscaped() + "</s>";
        doc.setHtml(html);
    } else if (msgtype == "m.text" || msgtype == "m.emote" || msgtype.isEmpty()) {
        std::string html = progressive::markdownToHtml(body.toStdString());
        if (html.empty()) doc.setPlainText(body);
        else doc.setHtml(QString::fromStdString(html));
    } else {
        doc.setPlainText(body);
    }
    doc.setTextWidth(r.width());

    p->save();
    p->translate(r.topLeft());
    QAbstractTextDocumentLayout::PaintContext ctx;
    ctx.palette = QGuiApplication::palette();
    // #f0f0f0 gives 5:1+ contrast on both #2a2a3e (incoming) and #0f3460 (outgoing)
    ctx.palette.setColor(QPalette::Text, Design::textColor);
    doc.documentLayout()->draw(p, ctx);
    p->restore();
}

} // namespace

TimelineDelegate::TimelineDelegate(ImageLoader* loader, QObject* parent)
    : QStyledItemDelegate(parent), loader_(loader) {}

// ---- Bubble drawing helpers ----

void TimelineDelegate::drawBubbleAvatar(QPainter* p, int x, int y,
                                          const QModelIndex& idx,
                                          const QString& senderId,
                                          const QString& senderName,
                                          const QString& avatarUrl) const {
    QRect r(x, y, AVATAR, AVATAR);
    QImage avatarImg;
    if (loader_ && !avatarUrl.isEmpty()) {
        if (loader_->hasImage(avatarUrl.toStdString())) {
            avatarImg = loader_->getCached(avatarUrl.toStdString());
        } else if (!pendingFetches_.count(avatarUrl.toStdString())) {
            QString av = avatarUrl;
            auto* self = const_cast<TimelineDelegate*>(this);
            self->pendingFetches_.insert(av.toStdString());
            auto* model = const_cast<QAbstractItemModel*>(idx.model());
            self->loader_->fetchThumbnail(
                av.toStdString(), AVATAR * 2, AVATAR * 2,
                [self, av, model](const QImage& img) {
                    self->pendingFetches_.erase(av.toStdString());
                    if (!img.isNull() && model) {
                        for (int i = 0; i < model->rowCount(); ++i) {
                            auto mi = model->index(i, 0);
                            if (mi.data(TimelineModel::AvatarUrlRole).toString() == av) {
                                emit model->dataChanged(mi, mi);
                                break;
                            }
                        }
                    }
                });
        }
    }

    if (!avatarImg.isNull()) {
        QImage scaled = avatarImg.scaled(AVATAR, AVATAR, Qt::KeepAspectRatioByExpanding,
                                         Qt::SmoothTransformation);
        QPainterPath path;
        path.addEllipse(r);
        p->save();
        p->setClipPath(path);
        p->drawImage(r, scaled);
        p->restore();
    } else {
        QColor color = colorFromId(senderId);
        p->setBrush(color);
        p->setPen(Qt::NoPen);
        p->drawEllipse(r);
        // Safe first character (handles emoji/surrogate pairs)
        QString letter = "?";
        if (!senderName.isEmpty()) {
            QChar first = senderName[0];
            if (first.isHighSurrogate() && senderName.size() > 1)
                letter = senderName.left(2);  // emoji pair
            else
                letter = QString(first.toUpper());
        }
        QFont f = p->font();
        f.setBold(true);
        f.setPointSize(11);
        p->setFont(f);
        p->setPen(Qt::white);
        p->drawText(r, Qt::AlignCenter, letter);
    }
}

void TimelineDelegate::drawBubble(QPainter* p, int x, int y, int w, int h,
                                    const QColor& color, int tlRadius, int trRadius,
                                    int brRadius, int blRadius) const {
    p->setPen(Qt::NoPen);
    p->setBrush(color);
    QPainterPath path;
    // Start top-left
    path.moveTo(x + tlRadius, y);
    // Top edge
    path.lineTo(x + w - trRadius, y);
    // Top-right corner
    if (trRadius > 0)
        path.arcTo(x + w - trRadius * 2, y, trRadius * 2, trRadius * 2, 90, -90);
    else
        path.lineTo(x + w, y);
    // Right edge
    path.lineTo(x + w, y + h - brRadius);
    // Bottom-right corner
    if (brRadius > 0)
        path.arcTo(x + w - brRadius * 2, y + h - brRadius * 2, brRadius * 2, brRadius * 2, 0, -90);
    else
        path.lineTo(x + w, y + h);
    // Bottom edge
    path.lineTo(x + blRadius, y + h);
    // Bottom-left corner
    if (blRadius > 0)
        path.arcTo(x, y + h - blRadius * 2, blRadius * 2, blRadius * 2, 270, -90);
    else
        path.lineTo(x, y + h);
    // Left edge
    path.lineTo(x, y + tlRadius);
    // Top-left corner
    if (tlRadius > 0)
        path.arcTo(x, y, tlRadius * 2, tlRadius * 2, 180, -90);
    else
        path.lineTo(x, y);
    path.closeSubpath();
    p->drawPath(path);
}

void TimelineDelegate::drawSystemRow(QPainter* p, const QRect& rect,
                                       const QModelIndex& idx,
                                       const QString& type) const {
    p->setPen(Design::systemTextColor);
    QFont f = p->font();
    f.setItalic(true);
    f.setPointSize(10);
    p->setFont(f);

    QString text;
    if (type == "m.room.member") {
        QString sender = idx.data(TimelineModel::SenderNameRole).toString();
        if (sender.isEmpty()) sender = shortName(idx.data(TimelineModel::SenderRole).toString());
        QString contentJson = idx.data(TimelineModel::ContentJsonRole).toString();
        // Parse membership with simdjson
        std::string cj = contentJson.toStdString();
        QString membership;
        simdjson::dom::parser pj;
        auto doc = pj.parse(cj);
        if (doc.error() == simdjson::SUCCESS) {
            auto ms = doc.value()["membership"].get_string();
            if (ms.error() == simdjson::SUCCESS) membership = QString::fromUtf8(ms.value().data(), (int)ms.value().size());
        }
        QString action;
        if (membership == "join") action = "joined";
        else if (membership == "leave") action = "left";
        else if (membership == "invite") action = "was invited";
        else if (membership == "ban") action = "was banned";
        else action = membership;
        int64_t ts = idx.data(TimelineModel::TimeRole).toLongLong();
        text = formatTime(ts) + " — " + sender + " " + action;
    } else if (type == "m.room.redaction") {
        QString sender = idx.data(TimelineModel::SenderNameRole).toString();
        if (sender.isEmpty()) sender = shortName(idx.data(TimelineModel::SenderRole).toString());
        int64_t ts = idx.data(TimelineModel::TimeRole).toLongLong();
        text = formatTime(ts) + " — " + sender + " redacted a message";
    }
    p->drawText(rect, Qt::AlignHCenter | Qt::AlignVCenter, text);
}

// ---- Bubble height calculation (shared between paint and sizeHint) ----

struct BubbleLayout {
    int nameH       = 0;
    int textH       = 0;
    int imageH      = 0;
    int pinnedH     = 0;
    int threadReplyH = 0;
    int threadCountH = 0;
    int reactionH   = 0;
    int bubbleH     = 0;
    bool isFirstInGroup = true;
    bool isLastInGroup  = true;
    bool isEmote     = false;

    int totalBubbleH() const {
        int h = nameH + PAD_TOP;
        if (pinnedH)     h += pinnedH;
        if (threadReplyH) h += threadReplyH;
        if (textH > 0)   h += textH + 4;
        if (imageH > 0)  h += imageH + 4;
        h += PAD_BOTTOM + TIME_ROW_H;
        if (threadCountH) h += threadCountH;
        // Reactions inside bubble only for non-last in group (merged bubbles)
        // Last in group draws reactions outside like old behavior
        if (reactionH && !isLastInGroup) h += reactionH;
        return h;
    }
};

static BubbleLayout computeLayout(const QModelIndex& idx,
                                   const QString& myUserId, int bubbleW) {
    BubbleLayout L;
    QString senderId = idx.data(TimelineModel::SenderRole).toString();
    QString senderName = idx.data(TimelineModel::SenderNameRole).toString();
    QString body = idx.data(TimelineModel::BodyRole).toString();
    QString msgtype = idx.data(TimelineModel::MsgTypeRole).toString();
    QString mxcUrl = idx.data(TimelineModel::MxcUrlRole).toString();
    bool imageLoaded = idx.data(TimelineModel::ImageLoadedRole).toBool();
    int threadCount = idx.data(TimelineModel::ThreadCountRole).toInt();
    bool isThreadReply = idx.data(TimelineModel::IsThreadReplyRole).toBool();
    bool pinned = idx.data(TimelineModel::IsPinnedRole).toBool();
    bool isOutgoing = !myUserId.isEmpty() && senderId == myUserId;
    bool isEmote = (msgtype == "m.emote");
    bool hasImage = (msgtype == "m.image" || msgtype == "m.video") && !mxcUrl.isEmpty();
    L.isEmote = isEmote;

    // O(1) group lookup — cached in DisplayedEvent by updateGroupMarkers
    L.isFirstInGroup = idx.data(TimelineModel::EventIdRole).isValid()
        ? idx.model()->data(idx, Qt::UserRole + 99).toBool() : true;
    // Read groupFirst/groupLast directly from model
    bool groupFirst = true, groupLast = true;
    int row = idx.row();
    auto* tm = static_cast<const TimelineModel*>(idx.model());
    if (tm && row >= 0 && row < tm->rowCount(QModelIndex())) {
        auto* evt = tm->at(row);
        if (evt) { groupFirst = evt->groupFirst; groupLast = evt->groupLast; }
    }
    L.isFirstInGroup = groupFirst || isEmote;
    L.isLastInGroup = groupLast || isEmote;

    if (!isOutgoing && !isEmote && L.isFirstInGroup && !senderName.isEmpty())
        L.nameH = 18;

    int textW = bubbleW - PAD * 2;
    if (!body.isEmpty() || isEmote) {
        QString text = isEmote ? ("* " + senderName + " " + body) : body;
        L.textH = calcTextHeight(text, isEmote ? "m.text" : msgtype, textW);
    }

    if (hasImage)
        L.imageH = imageLoaded ? 200 : 100;

    if (pinned)           L.pinnedH     = 14;
    if (isThreadReply)     L.threadReplyH = 14;
    if (threadCount > 0)  L.threadCountH = 16;

    // Reactions inside bubble — add height
    auto rxns = idx.data(TimelineModel::ReactionsRole).value<QStringList>();
    if (!rxns.isEmpty()) {
        int perRow = qMax(1, (bubbleW - PAD * 2) / 100);
        L.reactionH = qMin(2, ((int)rxns.size() + perRow - 1) / perRow) * 20;
    }

    L.bubbleH = L.totalBubbleH();
    return L;
}

void TimelineDelegate::drawMessageBubble(QPainter* p, const QRect& rowRect,
                                           const QModelIndex& idx) const {
    QString senderId = idx.data(TimelineModel::SenderRole).toString();
    QString senderName = idx.data(TimelineModel::SenderNameRole).toString();
    if (senderName.isEmpty()) senderName = shortName(senderId);
    QString avatarUrl = idx.data(TimelineModel::AvatarUrlRole).toString();
    QString body = idx.data(TimelineModel::BodyRole).toString();
    QString msgtype = idx.data(TimelineModel::MsgTypeRole).toString();
    QString mxcUrl = idx.data(TimelineModel::MxcUrlRole).toString();
    bool imageLoaded = idx.data(TimelineModel::ImageLoadedRole).toBool();
    bool isThreadReply = idx.data(TimelineModel::IsThreadReplyRole).toBool();
    int threadCount = idx.data(TimelineModel::ThreadCountRole).toInt();
    int64_t ts = idx.data(TimelineModel::TimeRole).toLongLong();
    bool pinned = idx.data(TimelineModel::IsPinnedRole).toBool();
    bool isReply = idx.data(TimelineModel::IsReplyRole).toBool();
    QString replyToEventId = idx.data(TimelineModel::ReplyToRole).toString();
    bool isOutgoing = !myUserId_.isEmpty() && senderId == myUserId_;
    bool isEmote = (msgtype == "m.emote");
    bool hasImage = (msgtype == "m.image" || msgtype == "m.video") && !mxcUrl.isEmpty();

    int avail = rowRect.width() - AVATAR - GAP - MARGIN * 2;
    int bubbleW = qMin(MAX_BUBBLE_W, avail);

    BubbleLayout L = computeLayout(idx, myUserId_, bubbleW);

    // Outgoing: avatar far right, then gap, then bubble
    // Incoming: avatar left, then gap, then bubble
    int bubbleX, avatarX;
    if (isOutgoing) {
        avatarX = rowRect.right() - MARGIN - AVATAR;
        bubbleX = avatarX - GAP - bubbleW;
    } else {
        avatarX = rowRect.x() + MARGIN;
        bubbleX = avatarX + AVATAR + GAP;
    }

    int topY = rowRect.y() + (L.isFirstInGroup ? 4 : SAME_SENDER_GAP);

    // Avatar — only first message in group
    if (!isEmote && L.isFirstInGroup) {
        drawBubbleAvatar(p, avatarX, topY, idx, senderId, senderName, avatarUrl);
    }

    // Sender name above bubble (incoming only, first in group)
    if (!isOutgoing && !isEmote && L.isFirstInGroup && !senderName.isEmpty()) {
        QFont nameFont = p->font();
        nameFont.setPointSize(10);
        nameFont.setBold(true);
        p->setFont(nameFont);
        p->setPen(colorFromId(senderId));
        p->drawText(bubbleX + PAD, topY, bubbleW - PAD, 16, Qt::AlignLeft | Qt::AlignBottom, senderName);
    }

    int bubbleY = topY + L.nameH;

    // Per-corner radii: only first/last in group rounded, middle = flat
    int r = RADIUS;
    int tl = L.isFirstInGroup ? r : 0;
    int tr = L.isFirstInGroup ? r : 0;
    int br = L.isLastInGroup ? r : 0;
    int bl = L.isLastInGroup ? r : 0;

    // Bubble background
    QColor bubbleColor = isOutgoing ? Design::outgoingBubble : Design::incomingBubble;
    if (isEmote) bubbleColor = QColor(0, 0, 0, 0);
    if (bubbleColor.alpha() > 0) {
        drawBubble(p, bubbleX, bubbleY, bubbleW, L.bubbleH, bubbleColor, tl, tr, br, bl);
    }

    int textX = bubbleX + PAD;
    int textW = bubbleW - PAD * 2;
    int curY = bubbleY + PAD_TOP;

    // Pinned
    if (pinned) {
        p->setPen(QColor("#ffaa00"));
        QFont f = p->font(); f.setPointSize(9); p->setFont(f);
        p->drawText(textX, curY, textW, 14, Qt::AlignLeft, "📌 Pinned");
        curY += 14;
    }

    // Thread reply indicator
    if (isThreadReply) {
        p->setPen(QColor("#6699cc"));
        QFont f = p->font(); f.setPointSize(9); p->setFont(f);
        p->drawText(textX, curY, textW, 14, Qt::AlignLeft, "🧵 thread reply");
        curY += 14;
    }

    // Reply preview — show small snippet of the original message
    if (isReply && !replyToEventId.isEmpty()) {
        const auto* model = static_cast<const TimelineModel*>(idx.model());
        int origRow = model->findRow(replyToEventId.toStdString());
        if (origRow >= 0) {
            auto* orig = model->at(origRow);
            if (orig) {
                QString preview = QString::fromStdString(orig->body).left(60);
                if (preview.isEmpty()) preview = "...";
                p->setPen(QColor("#888"));
                QFont f = p->font(); f.setPointSize(9); p->setFont(f);
                // Left border line
                p->setPen(QColor("#555"));
                p->drawLine(textX + 2, curY + 2, textX + 2, curY + 14);
                p->setPen(QColor("#888"));
                p->drawText(textX + 8, curY + 2, textW - 8, 14, Qt::AlignLeft,
                            QString::fromStdString(orig->senderName) + ": " + preview);
                curY += 14;
            }
        } else {
            p->setPen(QColor("#888"));
            QFont f = p->font(); f.setPointSize(9); p->setFont(f);
            p->drawText(textX, curY, textW, 14, Qt::AlignLeft, "↩ replied");
            curY += 14;
        }
    }

    // Body text — fills available space from curY to 18px above bubble bottom
    if (isEmote) {
        QString emoteText = "* " + senderName + " " + body;
        p->setPen(QColor("#c0c0c0"));
        QFont f = p->font(); f.setItalic(true); f.setPointSize(10); p->setFont(f);
        QRect emoteRect(bubbleX + PAD, bubbleY + 6, textW, L.textH + 20);
        p->drawText(emoteRect, Qt::AlignLeft | Qt::TextWordWrap, emoteText);
    } else if (!body.isEmpty()) {
        int textBottom = bubbleY + L.bubbleH - 18;
        int textH = textBottom - curY;
        if (textH < 20) textH = 20;
        if (msgtype == "m.file" || msgtype == "m.audio") {
            // File card: left border + icon + filename
            int cardH = 38;
            int cardW = qMin(textW, 250);
            QRect cardRect(textX, curY, cardW, cardH);
            p->setPen(QPen(QColor("#444"), 1));
            p->setBrush(QColor("#1e1e2e"));
            p->drawRoundedRect(cardRect, 6, 6);
            // Left accent border
            p->setPen(QPen(QColor(msgtype == "m.audio" ? "#4a6" : "#48a"), 3));
            p->drawLine(textX + 2, curY + 4, textX + 2, curY + cardH - 4);
            // Icon
            QFont iconFont = p->font(); iconFont.setPointSize(14); p->setFont(iconFont);
            p->setPen(QColor("#ccc"));
            p->drawText(textX + 12, curY + 4, 24, 30, Qt::AlignCenter,
                        msgtype == "m.audio" ? "🎵" : "📄");
            // Filename
            QFont nameF = p->font(); nameF.setPointSize(10); nameF.setBold(true);
            p->setFont(nameF);
            p->setPen(QColor("#ddd"));
            QFontMetrics nfm(nameF);
            p->drawText(textX + 40, curY + 6, cardW - 48, 18, Qt::AlignLeft,
                        nfm.elidedText(body, Qt::ElideMiddle, cardW - 48));
            // Type label
            QFont typeF = p->font(); typeF.setPointSize(8); p->setFont(typeF);
            p->setPen(QColor("#888"));
            p->drawText(textX + 40, curY + 22, cardW - 48, 14, Qt::AlignLeft,
                        msgtype == "m.audio" ? "Audio file" : "File");
            curY += cardH + 4;
        } else {
            drawRichText(p, QRect(textX, curY, textW, textH), body, msgtype);
            curY += textH;
        }
    }

    // Image
    if (hasImage) {
        curY += 2;
        int maxW = qMin(bubbleW - PAD * 2, 300);
        if (imageLoaded) {
            QImage img = idx.data(TimelineModel::ImageRole).value<QImage>();
            if (!img.isNull()) {
                QImage scaled = img.scaled(maxW, 200, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                QRect imgRect(bubbleX + PAD, curY, scaled.width(), scaled.height());
                p->drawImage(imgRect, scaled);
                curY += scaled.height() + 2;
                if (msgtype == "m.video") {
                    p->setPen(Qt::NoPen);
                    p->setBrush(QColor(255, 255, 255, 80));
                    QRect playBtn(imgRect.center().x() - 15, imgRect.center().y() - 15, 30, 30);
                    p->drawEllipse(playBtn);
                    p->setPen(Qt::white);
                    QFont pf = p->font(); pf.setPointSize(12); p->setFont(pf);
                    p->drawText(playBtn, Qt::AlignCenter, "▶");
                }
            }
        } else {
            QRect placeholderRect(bubbleX + PAD, curY, maxW, 100);
            p->setPen(QPen(QColor("#3a3a3a"), 1));
            p->setBrush(QColor("#1a1a1a"));
            p->drawRoundedRect(placeholderRect, 6, 6);
            p->setPen(QColor("#888"));
            QFont pf = p->font(); pf.setPointSize(10); p->setFont(pf);
            p->drawText(placeholderRect, Qt::AlignCenter,
                        msgtype == "m.video" ? "🎬 loading..." : "🖼 loading...");
            curY += 102;
            QString eventId = idx.data(TimelineModel::EventIdRole).toString();
            const_cast<TimelineDelegate*>(this)->loader_->fetchThumbnail(
                mxcUrl.toStdString(), 300, 200,
                [eventId, model = const_cast<QAbstractItemModel*>(idx.model())]
                (const QImage& img) {
                    if (!img.isNull() && model) {
                        auto* tm = qobject_cast<TimelineModel*>(model);
                        if (tm) tm->setImage(eventId.toStdString(), img);
                    }
                });
        }
    }

    // Thread reply count — right above timestamp, at the bottom of bubble
    if (threadCount > 0) {
        p->setPen(QColor("#6699cc"));
        QFont f = p->font(); f.setPointSize(9); p->setFont(f);
        int tcY = bubbleY + L.bubbleH - PAD_BOTTOM - TIME_ROW_H - L.threadCountH - 2;
        p->drawText(textX, tcY, textW, 14, Qt::AlignLeft,
                    QString("💬 %1 %2").arg(threadCount)
                        .arg(threadCount == 1 ? "reply" : "replies"));
    }

    // Timestamp — bottom-right inside bubble
    QString timeStr = formatTime(ts);
    if (!timeStr.isEmpty()) {
        p->setPen(QColor("#aaa"));
        QFont f = p->font(); f.setPointSize(9); p->setFont(f);
        QFontMetrics fm(f);
        int tw = fm.horizontalAdvance(timeStr);
        int ty = bubbleY + L.bubbleH - PAD_BOTTOM - TIME_ROW_H;
        p->drawText(bubbleX + bubbleW - tw - PAD, ty, tw, TIME_ROW_H,
                    Qt::AlignRight | Qt::AlignVCenter, timeStr);
    }

    // Reactions — inside bubble for group (merged), outside for standalone messages
    auto reactionsVar = idx.data(TimelineModel::ReactionsRole);
    QStringList reactions = reactionsVar.value<QStringList>();
    if (!reactions.isEmpty()) {
        int rx = bubbleX + PAD;
        int baseY;
        if (L.isLastInGroup) {
            baseY = bubbleY + L.bubbleH + 2;  // outside
        } else {
            baseY = bubbleY + L.bubbleH - PAD_BOTTOM - TIME_ROW_H - L.reactionH;
            if (threadCount > 0) baseY -= L.threadCountH + 2;
        }
        int ry = baseY;
        QFont pillFont = p->font(); pillFont.setPointSize(9); p->setFont(pillFont);
        QFontMetrics fm(pillFont);
        int maxX = bubbleX + bubbleW - PAD;
        int rowNum = 0;
        int shown = 0;
        for (int i = 0; i < reactions.size(); ++i) {
            const QString& pill = reactions[i];
            int pw = fm.horizontalAdvance(pill) + 16;
            if (rx + pw > maxX && shown > 0) {
                rx = bubbleX + PAD;
                ry += 20;
                rowNum++;
                if (rowNum >= 2) {
                    int left = static_cast<int>(reactions.size()) - shown;
                    if (left > 0) {
                        QString more = "+" + QString::number(left);
                        int mw = fm.horizontalAdvance(more) + 16;
                        QRect pr(rx, ry, mw, 20);
                        p->setPen(QColor("#3a3a3a"));
                        p->setBrush(QColor("#2a2a2a"));
                        p->drawRoundedRect(pr, 8, 8);
                        p->setPen(QColor("#e8e8e8"));
                        p->drawText(pr, Qt::AlignCenter, more);
                    }
                    break;
                }
            }
            if (rx + pw > maxX && shown == 0) pw = maxX - rx;
            QRect pr(rx, ry, pw, 20);
            p->setPen(QColor("#3a3a3a"));
            p->setBrush(QColor("#2a2a2a"));
            p->drawRoundedRect(pr, 8, 8);
            p->setPen(QColor("#e8e8e8"));
            p->drawText(pr, Qt::AlignCenter, pill);
            rx += pw + 3;
            shown++;
        }
    }
}

void TimelineDelegate::paint(QPainter* p, const QStyleOptionViewItem& opt,
                               const QModelIndex& idx) const {
    p->save();
    p->setRenderHint(QPainter::Antialiasing);

    if (opt.state & QStyle::State_Selected) {
        p->fillRect(opt.rect, Design::rowBgDark);
    } else {
        p->fillRect(opt.rect, Design::rowBgDark);
    }

    QString type = idx.data(TimelineModel::TypeRole).toString();
    if (type == "m.room.member" || type == "m.room.redaction") {
        drawSystemRow(p, opt.rect, idx, type);
    } else {
        drawMessageBubble(p, opt.rect, idx);
    }

    p->restore();
}

QSize TimelineDelegate::sizeHint(const QStyleOptionViewItem& opt,
                                   const QModelIndex& idx) const {
    QString type = idx.data(TimelineModel::TypeRole).toString();
    if (type == "m.room.member" || type == "m.room.redaction") {
        return QSize(opt.rect.width() > 40 ? opt.rect.width() : 600, 28);
    }

    int width = opt.rect.width();
    if (width <= 40) width = 600;
    int bubbleW = qMin(MAX_BUBBLE_W, width - AVATAR - GAP - MARGIN * 2);

    BubbleLayout L = computeLayout(idx, myUserId_, bubbleW);

    int totalH = (L.isFirstInGroup ? 4 : SAME_SENDER_GAP) + L.bubbleH + 2;
    return QSize(width, qMax(totalH, AVATAR + 8));
}

bool TimelineDelegate::editorEvent(QEvent* event, QAbstractItemModel* model,
                                     const QStyleOptionViewItem& option,
                                     const QModelIndex& index) {
    if (event->type() == QEvent::MouseButtonRelease) {
        auto* me = static_cast<QMouseEvent*>(event);
        QString eventId = index.data(TimelineModel::EventIdRole).toString();
        QString mxcUrl = index.data(TimelineModel::MxcUrlRole).toString();
        QString msgtype = index.data(TimelineModel::MsgTypeRole).toString();
        QString body = index.data(TimelineModel::BodyRole).toString();
        bool isReply = index.data(TimelineModel::IsReplyRole).toBool();
        QString replyTo = index.data(TimelineModel::ReplyToRole).toString();

        // Compute bubble position for hit zones
        int avail = option.rect.width() - AVATAR - GAP - MARGIN * 2;
        int bubbleW = qMin(MAX_BUBBLE_W, avail);
        QString senderId = index.data(TimelineModel::SenderRole).toString();
        bool isOutgoing = !myUserId_.isEmpty() && senderId == myUserId_;
        int bubbleX = isOutgoing ? (option.rect.right() - MARGIN - AVATAR - GAP - bubbleW)
                                  : (option.rect.x() + MARGIN + AVATAR + GAP);

        // Reaction zone: bottom of bubble
        auto rxns = index.data(TimelineModel::ReactionsRole).value<QStringList>();
        if (!rxns.isEmpty()) {
            int ry = option.rect.y() + option.rect.height() - 24;
            QRect reactionZone(bubbleX, ry, bubbleW, 22);
            if (reactionZone.contains(me->pos())) {
                // Show first reaction emoji as tooltip
                QString tip;
                for (const QString& r : rxns) tip += r + " ";
                emit reactionClicked(eventId, rxns.first());
                return true;
            }
        }

        // Reply preview zone — click to scroll to original
        if (isReply && !replyTo.isEmpty()) {
            auto* tm = static_cast<const TimelineModel*>(model);
            int origRow = tm->findRow(replyTo.toStdString());
            if (origRow >= 0) {
                QRect replyZone(bubbleX + PAD, option.rect.y() + 32, bubbleW - PAD*2, 16);
                if (replyZone.contains(me->pos())) {
                    emit messageClicked(replyTo);  // signal to scroll to original
                    return true;
                }
            }
        }

        // Image zone
        if ((msgtype == "m.image" || msgtype == "m.video") && !mxcUrl.isEmpty()) {
            // Image is drawn inside bubble — check if click is in lower half
            if (me->pos().y() > option.rect.y() + 60) {
                emit imageClicked(eventId, mxcUrl);
                return true;
            }
        }

        // Markdown link check
        if (msgtype == "m.text" || msgtype.isEmpty()) {
            QTextDocument doc;
            doc.setDefaultFont(QFont(QApplication::font().family(), 10));
            std::string html = progressive::markdownToHtml(body.toStdString());
            doc.setHtml(html.empty() ? body.toHtmlEscaped() : QString::fromStdString(html));
            doc.setTextWidth(bubbleW - PAD * 2);
            QPointF docPos(me->pos().x() - bubbleX - PAD, me->pos().y() - option.rect.y() - 40);
            QString anchor = doc.documentLayout()->anchorAt(docPos);
            if (!anchor.isEmpty()) {
                if (anchor.startsWith("https://") || anchor.startsWith("http://")) {
                    emit linkClicked(anchor);
                    return true;
                }
                if (anchor.startsWith("matrix.to")) {
                    emit linkClicked("https://" + anchor);
                    return true;
                }
            }
        }

        emit messageClicked(eventId);
        return true;
    }
    return QStyledItemDelegate::editorEvent(event, model, option, index);
}

QColor TimelineDelegate::avatarColor(const QString& userId) const {
    return colorFromId(userId);
}

} // namespace progressive::desktop
