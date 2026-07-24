// src/ui/timeline/timeline_painter.cpp — timeline rendering functions.
#include "timeline_painter.hpp"
#include "timeline_layout.hpp"
#include "timeline_model.hpp"
#include "../shared/image_loader.hpp"
#include "../shared/theme.hpp"

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QColor>
#include <QDateTime>
#include <QFont>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QPainterPath>
#include <QPalette>
#include <QTextDocument>
#include <progressive/markdown.hpp>

namespace progressive::desktop::timeline_render {

using namespace timeline_layout;

namespace {

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

void drawRichText(QPainter* p, const QRect& r, const QString& body,
                  const QString& msgtype, bool isOutgoing) {
    QTextDocument doc;
    doc.setDefaultFont(QFont(QApplication::font().family(), ds(kFontSizeBody)));
    if (msgtype == "m.notice" && body == "[Message deleted]") {
        QString html = "<s style='color:#666;'>" + body.toHtmlEscaped() + "</s>";
        doc.setHtml(html);
    } else if (msgtype == "m.text" || msgtype == "m.emote" || msgtype.isEmpty()) {
        std::string html = progressive::markdownToHtml(body.toStdString());
        if (html.empty()) doc.setPlainText(body);
        else doc.setHtml(QString::fromStdString(html));
        if (isOutgoing)
            doc.setDefaultStyleSheet("a { color: " + Design::linkOnOutgoing.name() + "; }");
    } else {
        doc.setPlainText(body);
    }
    doc.setTextWidth(r.width());

    p->save();
    p->translate(r.topLeft());
    QAbstractTextDocumentLayout::PaintContext ctx;
    ctx.palette = QGuiApplication::palette();
    ctx.palette.setColor(QPalette::Text, Design::textColor);
    doc.documentLayout()->draw(p, ctx);
    p->restore();
}

void drawBubble(QPainter* p, int x, int y, int w, int h,
                const QColor& color, int tlRadius, int trRadius,
                int brRadius, int blRadius) {
    p->setPen(Qt::NoPen);
    p->setBrush(color);
    QPainterPath path;
    path.moveTo(x + tlRadius, y);
    path.lineTo(x + w - trRadius, y);
    if (trRadius > 0)
        path.arcTo(x + w - trRadius * 2, y, trRadius * 2, trRadius * 2, 90, -90);
    else
        path.lineTo(x + w, y);
    path.lineTo(x + w, y + h - brRadius);
    if (brRadius > 0)
        path.arcTo(x + w - brRadius * 2, y + h - brRadius * 2, brRadius * 2, brRadius * 2, 0, -90);
    else
        path.lineTo(x + w, y + h);
    path.lineTo(x + blRadius, y + h);
    if (blRadius > 0)
        path.arcTo(x, y + h - blRadius * 2, blRadius * 2, blRadius * 2, 270, -90);
    else
        path.lineTo(x, y + h);
    path.lineTo(x, y + tlRadius);
    if (tlRadius > 0)
        path.arcTo(x, y, tlRadius * 2, tlRadius * 2, 180, -90);
    else
        path.lineTo(x, y);
    path.closeSubpath();
    p->drawPath(path);
}

void drawBubbleAvatar(QPainter* p, int x, int y,
                      const QModelIndex& idx,
                      const QString& senderId,
                      const QString& senderName,
                      const QString& avatarUrl,
                      ImageLoader* loader,
                      std::unordered_set<std::string>& pendingFetches) {
    QRect r(x, y, kAvatarSize, kAvatarSize);
    QImage avatarImg;
    if (loader && !avatarUrl.isEmpty()) {
        if (loader->hasImage(avatarUrl.toStdString())) {
            avatarImg = loader->getCached(avatarUrl.toStdString());
        } else if (!pendingFetches.count(avatarUrl.toStdString())) {
            QString av = avatarUrl;
            pendingFetches.insert(av.toStdString());
            auto* model = const_cast<QAbstractItemModel*>(idx.model());
            loader->fetchThumbnail(
                av.toStdString(), kAvatarSize * 2, kAvatarSize * 2,
                [&pendingFetches, av, model](const QImage& img) {
                    pendingFetches.erase(av.toStdString());
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
        QImage scaled = avatarImg.scaled(kAvatarSize, kAvatarSize, Qt::KeepAspectRatioByExpanding,
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
        QString letter = "?";
        if (!senderName.isEmpty()) {
            QChar first = senderName[0];
            if (first.isHighSurrogate() && senderName.size() > 1)
                letter = senderName.left(2);
            else
                letter = QString(first.toUpper());
        }
        QFont f = p->font();
        f.setBold(true);
        f.setPointSize(ds(kFontSizeName));
        p->setFont(f);
        p->setPen(Qt::white);
        p->drawText(r, Qt::AlignCenter, letter);
    }
}

} // namespace

void drawSystemRow(QPainter* p, const QRect& rect, const QModelIndex& idx,
                   const QString& type) {
    p->setPen(Design::systemTextColor);
    QFont f = p->font();
    f.setItalic(true);
    f.setPointSize(ds(kFontSizeBody));
    p->setFont(f);

    QString text;
    if (type == "progressive.system") {
        QString body = idx.data(TimelineModel::BodyRole).toString();
        int64_t ts = idx.data(TimelineModel::TimeRole).toLongLong();
        QString timeStr = formatTime(ts);
        text = timeStr.isEmpty() ? body : timeStr + "  " + body;
    }
    p->drawText(rect, Qt::AlignHCenter | Qt::AlignVCenter, text);
}

void drawMessageBubble(QPainter* p, const QRect& rowRect, const QModelIndex& idx,
                       const BubbleLayout& L, const QString& myUserId,
                       ImageLoader* loader,
                       std::unordered_set<std::string>& pendingFetches) {
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
    bool isOutgoing = !myUserId.isEmpty() && senderId == myUserId;
    bool isEmote = (msgtype == "m.emote");
    bool hasImage = (msgtype == "m.image" || msgtype == "m.video") && !mxcUrl.isEmpty();

    int avail = rowRect.width() - kAvatarSize - kGap - kMargin * 2;
    int bubbleW = qMin(kMaxBubbleW, avail);

    int bubbleX, avatarX;
    if (isOutgoing) {
        avatarX = rowRect.right() - kMargin - kAvatarSize;
        bubbleX = avatarX - kGap - bubbleW;
    } else {
        avatarX = rowRect.x() + kMargin;
        bubbleX = avatarX + kAvatarSize + kGap;
    }

    int topY = rowRect.y() + (L.isFirstInGroup ? 4 : kSameSenderGap);

    if (!isEmote && L.isFirstInGroup) {
        drawBubbleAvatar(p, avatarX, topY, idx, senderId, senderName, avatarUrl,
                         loader, pendingFetches);
    }

    if (!isOutgoing && !isEmote && L.isFirstInGroup && !senderName.isEmpty()) {
        QFont nameFont = p->font();
        nameFont.setPointSize(ds(kFontSizeBody));
        nameFont.setBold(true);
        p->setFont(nameFont);
        p->setPen(colorFromId(senderId));
        p->drawText(bubbleX + kBubblePadding, topY, bubbleW - kBubblePadding, kNameRowH,
                    Qt::AlignLeft | Qt::AlignBottom, senderName);
    }

    int bubbleY = topY + L.nameH;

    int r = kBubbleRadius;
    int tl = L.isFirstInGroup ? r : 0;
    int tr = L.isFirstInGroup ? r : 0;
    int br = L.isLastInGroup ? r : 0;
    int bl = L.isLastInGroup ? r : 0;

    QColor bubbleColor = isOutgoing ? Design::outgoingBubble : Design::incomingBubble;
    if (isEmote) bubbleColor = QColor(0, 0, 0, 0);
    if (bubbleColor.alpha() > 0) {
        drawBubble(p, bubbleX, bubbleY, bubbleW, L.bubbleH, bubbleColor, tl, tr, br, bl);
    }

    int textX = bubbleX + kBubblePadding;
    int textW = bubbleW - kBubblePadding * 2;
    int curY = bubbleY + kPadTop;

    if (pinned) {
        p->setPen(Design::pinnedColor);
        QFont f = p->font(); f.setPointSize(ds(kFontSizeSmall)); p->setFont(f);
        p->drawText(textX, curY, textW, 14, Qt::AlignLeft, "📌 Pinned");
        curY += 14;
    }

    if (isThreadReply) {
        p->setPen(Design::threadColor);
        QFont f = p->font(); f.setPointSize(ds(kFontSizeSmall)); p->setFont(f);
        p->drawText(textX, curY, textW, 14, Qt::AlignLeft, "🧵 thread reply");
        curY += 14;
    }

    if (isReply && !replyToEventId.isEmpty()) {
        const auto* model = static_cast<const TimelineModel*>(idx.model());
        int origRow = model->findRow(replyToEventId.toStdString());
        if (origRow >= 0) {
            auto* orig = model->at(origRow);
            if (orig) {
                QString preview = QString::fromStdString(orig->body).left(kReplyPreviewMax);
                if (preview.isEmpty()) preview = "...";
                QFont f = p->font(); f.setPointSize(ds(kFontSizeSmall)); p->setFont(f);
                p->setPen(Design::replyLineColor);
                p->drawLine(textX + 2, curY + 2, textX + 2, curY + 14);
                p->setPen(Design::mutedTextColor);
                p->drawText(textX + 8, curY + 2, textW - 8, 14, Qt::AlignLeft,
                            QString::fromStdString(orig->senderName) + ": " + preview);
                curY += 14;
            }
        } else {
            QFont f = p->font(); f.setPointSize(ds(kFontSizeSmall)); p->setFont(f);
            p->setPen(Design::mutedTextColor);
            p->drawText(textX, curY, textW, 14, Qt::AlignLeft, "↩ replied");
            curY += 14;
        }
    }

    if (isEmote) {
        QString emoteText = "* " + senderName + " " + body;
        p->setPen(Design::emoteColor);
        QFont f = p->font(); f.setItalic(true); f.setPointSize(ds(kFontSizeBody)); p->setFont(f);
        QRect emoteRect(bubbleX + kBubblePadding, bubbleY + 6, textW, L.textH + 14);
        p->drawText(emoteRect, Qt::AlignLeft | Qt::TextWordWrap, emoteText);
    } else if (!body.isEmpty()) {
        int textBottom = bubbleY + L.bubbleH - kEmoteBottomPad;
        int textH = textBottom - curY;
        if (textH < kMinTextH) textH = kMinTextH;
        if (msgtype == "m.file" || msgtype == "m.audio") {
            int cardH = kFileCardH;
            int cardW = qMin(textW, kFileCardMaxW);
            QRect cardRect(textX, curY, cardW, cardH);
            p->setPen(QPen(Design::fileCardBorder, 1));
            p->setBrush(QColor("#1e1e2e"));
            p->drawRoundedRect(cardRect, 6, 6);
            p->setPen(QPen(QColor(msgtype == "m.audio" ? "#4a6" : "#48a"), 3));
            p->drawLine(textX + 2, curY + 4, textX + 2, curY + cardH - 4);
            QFont iconFont = p->font(); iconFont.setPointSize(ds(kFontSizeEmoji)); p->setFont(iconFont);
            p->setPen(Design::fileCardIconText);
            p->drawText(textX + 12, curY + 4, 24, 30, Qt::AlignCenter,
                        msgtype == "m.audio" ? "🎵" : "📄");
            QFont nameF = p->font(); nameF.setPointSize(ds(kFontSizeBody)); nameF.setBold(true);
            p->setFont(nameF);
            p->setPen(Design::fileCardFileName);
            QFontMetrics nfm(nameF);
            p->drawText(textX + kFileCardTextX, curY + kFileCardTextY, cardW - kFileCardTextW, kFileCardTextH, Qt::AlignLeft,
                        nfm.elidedText(body, Qt::ElideMiddle, cardW - 48));
            QFont typeF = p->font(); typeF.setPointSize(ds(kFontSizeCaption)); p->setFont(typeF);
            p->setPen(Design::mutedTextColor);
            p->drawText(textX + kFileCardTextX, curY + kFileCardTypeY, cardW - kFileCardTextW, kIndicatorRowH, Qt::AlignLeft,
                        msgtype == "m.audio" ? "Audio file" : "File");
            curY += cardH + 4;
        } else {
            drawRichText(p, QRect(textX, curY, textW, textH), body, msgtype, isOutgoing);
            curY += textH;
        }
    }

    if (hasImage) {
        curY += 2;
        int maxW = qMin(bubbleW - kBubblePadding * 2, kMaxImageW);
        if (imageLoaded) {
            QImage img = idx.data(TimelineModel::ImageRole).value<QImage>();
            if (!img.isNull()) {
                QImage scaled = img.scaled(maxW, kImageLoadedH, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                QRect imgRect(bubbleX + kBubblePadding, curY, scaled.width(), scaled.height());
                p->drawImage(imgRect, scaled);
                curY += scaled.height() + 2;
                if (msgtype == "m.video") {
                    p->setPen(Qt::NoPen);
                    p->setBrush(QColor(255, 255, 255, 80));
                    QRect playBtn(imgRect.center().x() - 15, imgRect.center().y() - 15, 30, 30);
                    p->drawEllipse(playBtn);
                    p->setPen(Qt::white);
                    QFont pf = p->font(); pf.setPointSize(ds(kFontSizeIcon)); p->setFont(pf);
                    p->drawText(playBtn, Qt::AlignCenter, "▶");
                }
            }
        } else {
            QRect placeholderRect(bubbleX + kBubblePadding, curY, maxW, kImagePlaceholderH);
            p->setPen(QPen(Design::reactionBorder, 1));
            p->setBrush(Design::imgPlaceholderBg);
            p->drawRoundedRect(placeholderRect, 6, 6);
            p->setPen(Design::imgPlaceholderText);
            QFont pf = p->font(); pf.setPointSize(ds(kFontSizeBody)); p->setFont(pf);
            p->drawText(placeholderRect, Qt::AlignCenter,
                        msgtype == "m.video" ? "🎬 loading..." : "🖼 loading...");
            curY += kImagePlaceholderH + 2;
            QString eventId = idx.data(TimelineModel::EventIdRole).toString();
            loader->fetchThumbnail(
                mxcUrl.toStdString(), kMaxImageW, kImageLoadedH,
                [eventId, model = const_cast<QAbstractItemModel*>(idx.model())]
                (const QImage& img) {
                    if (!img.isNull() && model) {
                        auto* tm = qobject_cast<TimelineModel*>(model);
                        if (tm) tm->setImage(eventId.toStdString(), img);
                    }
                });
        }
    }

    if (threadCount > 0) {
        p->setPen(Design::threadColor);
        QFont f = p->font(); f.setPointSize(ds(kFontSizeSmall)); p->setFont(f);
        int tcY = bubbleY + L.bubbleH - kPadBottom - kTimeRowH - L.threadCountH - 2;
        p->drawText(textX, tcY, textW, 14, Qt::AlignLeft,
                    QString("💬 %1 %2").arg(threadCount)
                        .arg(threadCount == 1 ? "reply" : "replies"));
    }

    QString timeStr = formatTime(ts);
    if (!timeStr.isEmpty()) {
        p->setPen(Design::timeColor);
        QFont f = p->font(); f.setPointSize(ds(kFontSizeSmall)); p->setFont(f);
        QFontMetrics fm(f);
        int tw = fm.horizontalAdvance(timeStr);
        int ty = bubbleY + L.bubbleH - kPadBottom - kTimeRowH;
        p->drawText(bubbleX + bubbleW - tw - kBubblePadding, ty, tw, kTimeRowH,
                    Qt::AlignRight | Qt::AlignVCenter, timeStr);
    }

    auto reactionsVar = idx.data(TimelineModel::ReactionsRole);
    QStringList reactions = reactionsVar.value<QStringList>();
    if (!reactions.isEmpty()) {
        int baseY;
        if (L.isLastInGroup) {
            baseY = bubbleY + L.bubbleH + kBalloonBuf;
        } else {
            baseY = bubbleY + L.bubbleH - kPadBottom - kTimeRowH - L.reactionH;
            if (threadCount > 0) baseY -= L.threadCountH + kBalloonBuf;
        }
        QFont pillFont = p->font(); pillFont.setPointSize(ds(kFontSizeSmall)); p->setFont(pillFont);
        QFontMetrics fm(pillFont);
        auto rows = computeReactionLayout(reactions, bubbleX, baseY, bubbleW, fm);
        for (const auto& row : rows) {
            if (row.isOverflow) {
                p->setPen(Design::reactionBorder);
                p->setBrush(Design::reactionBg);
                p->drawRoundedRect(row.rect, 8, 8);
                p->setPen(Design::reactionTextColor);
                p->drawText(row.rect, Qt::AlignCenter, row.text);
            } else {
                p->setPen(QColor("#3a3a3a"));
                p->setBrush(QColor("#2a2a2a"));
                p->drawRoundedRect(row.rect, 8, 8);
                p->setPen(Design::reactionTextColor);
                p->drawText(row.rect, Qt::AlignCenter, row.text);
            }
        }
    }
}

} // namespace progressive::desktop::timeline_render
