// src/ui/timeline_delegate.cpp — chat bubbles with avatars.
#include "timeline_delegate.hpp"
#include "timeline_model.hpp"
#include "image_loader.hpp"

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
#include <QPointer>

#include <progressive/markdown.hpp>

namespace progressive::desktop {

namespace {

// ---- Layout constants ----
static const int AVATAR = 36;
static const int MARGIN = 8;
static const int GAP = 8;
static const int PAD = 10;
static const int RADIUS = 12;
static const int MAX_BUBBLE_W = 480;
static const int SAME_SENDER_GAP = 2;

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
    return QColor::fromHsl(hue, static_cast<int>(200 * 0.9), 150);
}

// ---- Text layout helpers ----

int textHeight(const QString& body, const QString& msgtype, int width) {
    if (body.isEmpty()) return 20;
    QTextDocument doc;
    doc.setDefaultFont(QFont("sans-serif", 10));
    if (msgtype == "m.text" || msgtype == "m.emote" || msgtype.isEmpty()) {
        std::string html = progressive::markdownToHtml(body.toStdString());
        if (html.empty()) doc.setPlainText(body);
        else doc.setHtml(QString::fromStdString(html));
    } else {
        doc.setPlainText(body);
    }
    doc.setTextWidth(width - PAD * 2);
    return static_cast<int>(doc.size().height()) + 4;
}

void drawRichText(QPainter* p, const QRect& r, const QString& body,
                  const QString& msgtype) {
    QTextDocument doc;
    doc.setDefaultFont(QFont("sans-serif", 10));
    if (msgtype == "m.text" || msgtype == "m.emote" || msgtype.isEmpty()) {
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
    ctx.palette.setColor(QPalette::Text, QColor("#d0d0d0"));
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
        } else {
            QString av = avatarUrl;
            auto* model = const_cast<QAbstractItemModel*>(idx.model());
            const_cast<TimelineDelegate*>(this)->loader_->fetchThumbnail(
                av.toStdString(), AVATAR * 2, AVATAR * 2,
                [av, model](const QImage& img) {
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
        QString letter = senderName.isEmpty() ? QString("?") : QString(senderName[0].toUpper());
        QFont f = p->font();
        f.setBold(true);
        f.setPointSize(11);
        p->setFont(f);
        p->setPen(Qt::white);
        p->drawText(r, Qt::AlignCenter, letter);
    }
}

void TimelineDelegate::drawBubble(QPainter* p, int x, int y, int w, int h,
                                    const QColor& color) const {
    p->setPen(Qt::NoPen);
    p->setBrush(color);
    p->drawRoundedRect(x, y, w, h, RADIUS, RADIUS);
}

void TimelineDelegate::drawSystemRow(QPainter* p, const QRect& rect,
                                       const QModelIndex& idx,
                                       const QString& type) const {
    p->setPen(QColor("#777"));
    QFont f = p->font();
    f.setItalic(true);
    f.setPointSize(10);
    p->setFont(f);

    QString text;
    if (type == "m.room.member") {
        QString sender = idx.data(TimelineModel::SenderNameRole).toString();
        if (sender.isEmpty()) sender = shortName(idx.data(TimelineModel::SenderRole).toString());
        QString contentJson = idx.data(TimelineModel::ContentJsonRole).toString();
        QString membership;
        auto pos = contentJson.indexOf("\"membership\":\"");
        if (pos >= 0) {
            auto start = pos + 14;
            auto end = contentJson.indexOf('"', start);
            if (end > start) membership = contentJson.mid(start, end - start);
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

void TimelineDelegate::drawMessageBubble(QPainter* p, const QRect& rowRect,
                                           const QModelIndex& idx) const {
    QString senderId = idx.data(TimelineModel::SenderRole).toString();
    QString senderName = idx.data(TimelineModel::SenderNameRole).toString();
    if (senderName.isEmpty()) senderName = shortName(senderId);
    QString avatarUrl = idx.data(TimelineModel::AvatarUrlRole).toString();
    QString body = idx.data(TimelineModel::BodyRole).toString();
    QString msgtype = idx.data(TimelineModel::MsgTypeRole).toString();
    QString mxcUrl = idx.data(TimelineModel::MxcUrlRole).toString();
    QString mimetype = idx.data(TimelineModel::MimetypeRole).toString();
    bool imageLoaded = idx.data(TimelineModel::ImageLoadedRole).toBool();
    bool isThreadReply = idx.data(TimelineModel::IsThreadReplyRole).toBool();
    int threadCount = idx.data(TimelineModel::ThreadCountRole).toInt();
    int64_t ts = idx.data(TimelineModel::TimeRole).toLongLong();
    bool pinned = idx.data(TimelineModel::IsPinnedRole).toBool();

    bool isOutgoing = !myUserId_.isEmpty() && senderId == myUserId_;
    bool isEmote = (msgtype == "m.emote");
    bool hasImage = (msgtype == "m.image" || msgtype == "m.video") && !mxcUrl.isEmpty();

    // Bubble width
    int avail = rowRect.width() - AVATAR - GAP - MARGIN * 2;
    int bubbleW = qMin(MAX_BUBBLE_W, avail);

    // Calculate bubble height
    int nameH = 0;
    if (!isOutgoing && !isEmote && !senderName.isEmpty()) {
        nameH = 18;
    }
    int contentH = 8; // padding top
    if (hasImage) {
        contentH += imageLoaded ? 200 : 120;
        contentH += 4;
    }
    if (pinned) contentH += 14;
    if (isThreadReply) contentH += 14;
    if (threadCount > 0) contentH += 16;
    if (!body.isEmpty() || isEmote) {
        if (isEmote) {
            contentH += textHeight("* " + senderName + " " + body, "m.text", bubbleW - PAD * 2);
        } else {
            contentH += textHeight(body, msgtype, bubbleW - PAD * 2);
        }
        contentH += 4;
    }
    contentH += 6; // padding bottom for time

    int bubbleH = nameH + contentH;

    // Align: outgoing = right, incoming = left
    int bubbleX, avatarX;

    if (isOutgoing) {
        bubbleX = rowRect.right() - MARGIN - bubbleW;
        avatarX = rowRect.right() - MARGIN - AVATAR;
    } else {
        bubbleX = rowRect.x() + MARGIN + AVATAR + GAP;
        avatarX = rowRect.x() + MARGIN;
    }

    // Consecutive message gap check
    int prevRow = idx.row() - 1;
    bool sameSender = false;
    if (prevRow >= 0) {
        auto prevIdx = idx.model()->index(prevRow, 0);
        if (prevIdx.isValid()) {
            sameSender = (prevIdx.data(TimelineModel::SenderRole).toString() == senderId);
        }
    }

    int topY = rowRect.y() + (sameSender ? SAME_SENDER_GAP : 4);

    // Draw avatar (incoming on left, outgoing on right)
    if (!isEmote && !sameSender) {
        drawBubbleAvatar(p, avatarX, topY, idx, senderId, senderName, avatarUrl);
    }

    // Sender name above bubble (incoming only, first in group)
    if (!isOutgoing && !isEmote && !sameSender && !senderName.isEmpty()) {
        QFont nameFont = p->font();
        nameFont.setPointSize(10);
        nameFont.setBold(true);
        p->setFont(nameFont);
        p->setPen(colorFromId(senderId));
        p->drawText(bubbleX + PAD, topY + 1, bubbleW - PAD, 16, Qt::AlignLeft, senderName);
    }

    int bubbleY = topY + nameH;

    // Bubble background
    QColor bubbleColor = isOutgoing ? QColor("#0f3460") : QColor("#2a2a3e");
    if (isEmote) bubbleColor = QColor(0, 0, 0, 0); // no bubble for emotes
    if (bubbleColor.alpha() > 0) {
        drawBubble(p, bubbleX, bubbleY, bubbleW, bubbleH, bubbleColor);
    }

    int textX = bubbleX + PAD;
    int textY = bubbleY + 6;
    int textW = bubbleW - PAD * 2;

    // Pinned indicator
    if (pinned) {
        p->setPen(QColor("#ffaa00"));
        QFont pinFont = p->font();
        pinFont.setPointSize(9);
        p->setFont(pinFont);
        p->drawText(textX, textY, textW, 14, Qt::AlignLeft, "📌 Pinned");
        textY += 14;
    }

    // Thread reply indicator
    if (isThreadReply) {
        p->setPen(QColor("#6699cc"));
        QFont threadFont = p->font();
        threadFont.setPointSize(9);
        p->setFont(threadFont);
        p->drawText(textX, textY, textW, 14, Qt::AlignLeft, "🧵 thread reply");
        textY += 14;
    }

    // Message body
    if (isEmote) {
        QString emoteText = "* " + senderName + " " + body;
        p->setPen(QColor("#c0c0c0"));
        QFont emoteFont = p->font();
        emoteFont.setItalic(true);
        emoteFont.setPointSize(10);
        p->setFont(emoteFont);
        p->drawText(bubbleX + PAD, bubbleY + 4, textW, bubbleH - 8,
                    Qt::AlignLeft | Qt::TextWordWrap, emoteText);
    } else if (!body.isEmpty()) {
        drawRichText(p, QRect(textX, textY, textW, bubbleH - (textY - bubbleY) - 20),
                     body, msgtype);
    }

    // Image thumbnail
    if (hasImage) {
        int imgY = bubbleY + (isEmote ? 4 : (textY - bubbleY)) + (body.isEmpty() ? 4 : textHeight(body, msgtype, bubbleW - PAD * 2) + 8);
        int maxW = qMin(bubbleW - PAD * 2, 300);
        if (imageLoaded) {
            QImage img = idx.data(TimelineModel::ImageRole).value<QImage>();
            if (!img.isNull()) {
                QImage scaled = img.scaled(maxW, 200, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                QRect imgRect(bubbleX + PAD, imgY, scaled.width(), scaled.height());
                p->drawImage(imgRect, scaled);
                if (msgtype == "m.video") {
                    p->setPen(Qt::NoPen);
                    p->setBrush(QColor(0, 0, 0, 100));
                    QRect playBtn(imgRect.center().x() - 15, imgRect.center().y() - 15, 30, 30);
                    p->drawEllipse(playBtn);
                    p->setPen(Qt::white);
                    QFont playFont = p->font();
                    playFont.setPointSize(12);
                    p->setFont(playFont);
                    p->drawText(playBtn, Qt::AlignCenter, "▶");
                }
            }
        } else {
            QRect placeholderRect(bubbleX + PAD, imgY, maxW, 100);
            p->setPen(QPen(QColor("#3a3a3a"), 1));
            p->setBrush(QColor("#1a1a1a"));
            p->drawRoundedRect(placeholderRect, 6, 6);
            p->setPen(QColor("#888"));
            QFont placeholderFont = p->font();
            placeholderFont.setPointSize(10);
            p->setFont(placeholderFont);
            p->drawText(placeholderRect, Qt::AlignCenter,
                        msgtype == "m.video" ? "🎬 loading..." : "🖼 loading...");
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

    // Thread reply count
    if (threadCount > 0) {
        int tY = bubbleY + bubbleH - 34;
        p->setPen(QColor("#6699cc"));
        QFont tcFont = p->font();
        tcFont.setPointSize(9);
        p->setFont(tcFont);
        p->drawText(textX, tY, textW, 14, Qt::AlignLeft,
                    QString("💬 %1 %2").arg(threadCount)
                        .arg(threadCount == 1 ? "reply" : "replies"));
    }

    // Timestamp — bottom-right inside bubble
    QString timeStr = formatTime(ts);
    if (!timeStr.isEmpty()) {
        p->setPen(QColor("#888"));
        QFont timeFont = p->font();
        timeFont.setPointSize(9);
        p->setFont(timeFont);
        QFontMetrics fm(timeFont);
        int tw = fm.horizontalAdvance(timeStr);
        p->drawText(bubbleX + bubbleW - tw - 6, bubbleY + bubbleH - 6 - fm.height() + fm.ascent(), timeStr);
    }

    // Reactions pills below bubble
    auto reactionsVar = idx.data(TimelineModel::ReactionsRole);
    QStringList reactions = reactionsVar.value<QStringList>();
    if (!reactions.isEmpty()) {
        int rx = bubbleX;
        int ry = bubbleY + bubbleH + 2;
        QFont pillFont = p->font();
        pillFont.setPointSize(9);
        p->setFont(pillFont);
        QFontMetrics fm(pillFont);
        for (const QString& pill : reactions) {
            int textWidth = fm.horizontalAdvance(pill) + 6;
            QRect pillRect(rx, ry, textWidth + 10, 18);
            p->setPen(QColor("#3a3a3a"));
            p->setBrush(QColor("#2a2a2a"));
            p->drawRoundedRect(pillRect, 8, 8);
            p->setPen(QColor("#e8e8e8"));
            p->drawText(pillRect, Qt::AlignCenter, pill);
            rx += pillRect.width() + 3;
            if (rx > bubbleX + bubbleW - 60) break;
        }
    }
}

void TimelineDelegate::paint(QPainter* p, const QStyleOptionViewItem& opt,
                               const QModelIndex& idx) const {
    p->save();
    p->setRenderHint(QPainter::Antialiasing);

    // Row background
    if (opt.state & QStyle::State_Selected) {
        p->fillRect(opt.rect, QColor(50, 80, 130));
    } else {
        p->fillRect(opt.rect, QColor(20, 20, 20));
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
    QString msgtype = idx.data(TimelineModel::MsgTypeRole).toString();
    QString body = idx.data(TimelineModel::BodyRole).toString();
    QString senderId = idx.data(TimelineModel::SenderRole).toString();
    QString mxcUrl = idx.data(TimelineModel::MxcUrlRole).toString();
    bool imageLoaded = idx.data(TimelineModel::ImageLoadedRole).toBool();

    int width = opt.rect.width();
    if (width <= 40) width = 600;

    if (type == "m.room.member" || type == "m.room.redaction") {
        return QSize(width, 28);
    }

    int bubbleW = qMin(MAX_BUBBLE_W, width - AVATAR - GAP - MARGIN * 2);

    // Same sender gap?
    int prevRow = idx.row() - 1;
    bool sameSender = false;
    if (prevRow >= 0) {
        auto prevIdx = idx.model()->index(prevRow, 0);
        if (prevIdx.isValid()) {
            sameSender = (prevIdx.data(TimelineModel::SenderRole).toString() == senderId);
        }
    }

    bool isOutgoing = !myUserId_.isEmpty() && senderId == myUserId_;
    bool isEmote = (msgtype == "m.emote");
    bool hasImage = (msgtype == "m.image" || msgtype == "m.video") && !mxcUrl.isEmpty();

    int nameH = (!isOutgoing && !isEmote && !sameSender) ? 18 : 0;
    int contentH = 8;

    if (hasImage) contentH += (imageLoaded ? 200 : 120) + 4;
    if (!body.isEmpty() || isEmote) {
        contentH += textHeight(isEmote ? ("* " + senderId + " " + body) : body,
                               isEmote ? "m.text" : msgtype,
                               bubbleW - PAD * 2);
        contentH += 4;
    }
    contentH += 10; // time + padding

    // Thread indicators
    if (idx.data(TimelineModel::IsThreadReplyRole).toBool()) contentH += 14;
    if (idx.data(TimelineModel::ThreadCountRole).toInt() > 0) contentH += 16;
    if (idx.data(TimelineModel::IsPinnedRole).toBool()) contentH += 14;

    int bubbleH = nameH + contentH;

    // Reactions
    auto rxns = idx.data(TimelineModel::ReactionsRole).value<QStringList>();
    int reactionH = rxns.isEmpty() ? 0 : 22;

    int totalH = (sameSender ? SAME_SENDER_GAP : 4) + bubbleH + reactionH + 2;
    return QSize(width, qMax(totalH, AVATAR + 8));
}

bool TimelineDelegate::editorEvent(QEvent* event, QAbstractItemModel* model,
                                     const QStyleOptionViewItem& option,
                                     const QModelIndex& index) {
    if (event->type() == QEvent::MouseButtonRelease) {
        auto* me = static_cast<QMouseEvent*>(event);
        (void)me;
        (void)option;
        QString eventId = index.data(TimelineModel::EventIdRole).toString();
        QString mxcUrl = index.data(TimelineModel::MxcUrlRole).toString();
        QString msgtype = index.data(TimelineModel::MsgTypeRole).toString();

        if ((msgtype == "m.image" || msgtype == "m.video") && !mxcUrl.isEmpty()) {
            emit imageClicked(eventId, mxcUrl);
        } else {
            emit messageClicked(eventId);
        }
        return true;
    }
    return QStyledItemDelegate::editorEvent(event, model, option, index);
}

QColor TimelineDelegate::avatarColor(const QString& userId) const {
    return colorFromId(userId);
}

} // namespace progressive::desktop
