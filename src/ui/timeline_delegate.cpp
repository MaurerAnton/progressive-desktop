// src/ui/timeline_delegate.cpp
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

QString formatTime(int64_t ts) {
    if (ts <= 0) return "?";
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

// Generate a deterministic color from a user ID.
QColor colorFromId(const QString& id) {
    uint hash = 0;
    for (QChar c : id) hash = hash * 31 + c.unicode();
    int hue = static_cast<int>(hash % 360);
    return QColor::fromHsl(hue, 120, 130);
}

} // namespace

TimelineDelegate::TimelineDelegate(ImageLoader* loader, QObject* parent)
    : QStyledItemDelegate(parent), loader_(loader) {}

QString TimelineDelegate::formatMessageHtml(const QModelIndex& index) const {
    QString sender = index.data(TimelineModel::SenderNameRole).toString();
    if (sender.isEmpty()) sender = shortName(index.data(TimelineModel::SenderRole).toString());
    QString body = index.data(TimelineModel::BodyRole).toString();
    QString type = index.data(TimelineModel::TypeRole).toString();
    QString msgtype = index.data(TimelineModel::MsgTypeRole).toString();
    int64_t ts = index.data(TimelineModel::TimeRole).toLongLong();
    bool pinned = index.data(TimelineModel::IsPinnedRole).toBool();
    int threadCount = index.data(TimelineModel::ThreadCountRole).toInt();
    bool isThreadReply = index.data(TimelineModel::IsThreadReplyRole).toBool();

    QString timeStr = formatTime(ts);
    QString pinIcon = pinned ? "📌 " : "";
    // Thread reply: add left margin + 🧵 marker
    QString leftMargin = isThreadReply ? "margin-left:30px;" : "margin:2px 0;";
    QString threadPrefix = isThreadReply ? "🧵 <i style='color:#6699cc'>thread reply</i><br>" : "";
    // Thread root: show "💬 N replies" bubble below the message
    QString threadBubble = threadCount > 0
        ? QString("<br><span style='color:#6699cc;font-size:smaller'>💬 %1 replies</span>")
            .arg(threadCount)
        : "";

    if (type == "m.room.message") {
        QString bodyHtml;
        if (msgtype == "m.text" || msgtype.isEmpty()) {
            std::string rendered = progressive::markdownToHtml(body.toStdString());
            bodyHtml = rendered.empty() ? body.toHtmlEscaped() : QString::fromStdString(rendered);
        } else if (msgtype == "m.emote") {
            bodyHtml = QString("<i>* %1 %2</i>").arg(sender.toHtmlEscaped(), body.toHtmlEscaped());
            sender = "&nbsp;";
        } else if (msgtype == "m.notice") {
            bodyHtml = QString("<span style='color:#969696'>%1</span>").arg(body.toHtmlEscaped());
        } else if (msgtype == "m.image" || msgtype == "m.video" ||
                   msgtype == "m.audio" || msgtype == "m.file") {
            bodyHtml = QString("<i>[%1]</i>").arg(msgtype.mid(2));
        } else {
            bodyHtml = body.toHtmlEscaped();
        }
        return QString("<p style='%1'>%2<b style='color:%3'>%4</b> "
                       "<span style='color:#969696;font-size:smaller'>%5</span>%6<br>%7%8</p>")
            .arg(leftMargin, threadPrefix,
                 colorFromId(index.data(TimelineModel::SenderRole).toString()).name(),
                 sender.toHtmlEscaped(), timeStr, pinIcon, bodyHtml, threadBubble);
    } else if (type == "m.room.encrypted") {
        return QString("<p style='%1;color:#969696'><b>%2</b> %3<br>"
                       "<i>[encrypted]</i></p>")
            .arg(leftMargin, sender.toHtmlEscaped(), timeStr);
    } else if (type == "m.room.member") {
        QString contentJson = index.data(TimelineModel::ContentJsonRole).toString();
        QString membership;
        // Simple extraction
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
        return QString("<p style='margin:2px 0;color:#969696;font-size:smaller'>%1 — %2 %3</p>")
            .arg(timeStr, sender.toHtmlEscaped(), action);
    } else if (type == "m.room.redaction") {
        return QString("<p style='margin:2px 0;color:#969696;font-size:smaller'>%1 — %2 redacted a message</p>")
            .arg(timeStr, sender.toHtmlEscaped());
    }
    return QString("<p style='color:#969696'>%1</p>").arg(type.toHtmlEscaped());
}

void TimelineDelegate::drawAvatar(QPainter* painter, const QRect& rect,
                                    const QString& userId, const QString& name,
                                    const QImage& avatarImg) const {
    if (!avatarImg.isNull()) {
        // Draw real avatar as circle
        QImage scaled = avatarImg.scaled(rect.size(), Qt::KeepAspectRatioByExpanding,
                                          Qt::SmoothTransformation);
        QPainterPath path;
        path.addEllipse(rect);
        painter->save();
        painter->setClipPath(path);
        painter->drawImage(rect, scaled);
        painter->restore();
        return;
    }
    // Fallback: colored circle with letter
    QColor color = colorFromId(userId);
    painter->setBrush(color);
    painter->setPen(Qt::NoPen);
    painter->drawEllipse(rect);

    QString letter = name.isEmpty() ? QString("?") : QString(name[0].toUpper());
    QFont font = painter->font();
    font.setBold(true);
    font.setPointSize(10);
    painter->setFont(font);
    painter->setPen(Qt::white);
    painter->drawText(rect, Qt::AlignCenter, letter);
}

void TimelineDelegate::renderText(QPainter* painter, const QRect& rect, const QString& html) const {
    auto doc = std::make_shared<QTextDocument>();
    doc->setHtml(html);
    doc->setTextWidth(rect.width());

    painter->save();
    painter->translate(rect.topLeft());
    QAbstractTextDocumentLayout::PaintContext ctx;
    ctx.palette = QGuiApplication::palette();
    ctx.palette.setColor(QPalette::Text, QColor("#e8e8e8"));
    doc->documentLayout()->draw(painter, ctx);
    painter->restore();
}

void TimelineDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                               const QModelIndex& index) const {
    painter->save();

    // Background
    if (option.state & QStyle::State_Selected) {
        painter->fillRect(option.rect, QColor(50, 80, 130));
    } else if (index.row() % 2 == 0) {
        painter->fillRect(option.rect, QColor(30, 30, 30));
    } else {
        painter->fillRect(option.rect, QColor(20, 20, 20));
    }

    QString type = index.data(TimelineModel::TypeRole).toString();
    QString msgtype = index.data(TimelineModel::MsgTypeRole).toString();
    QString sender = index.data(TimelineModel::SenderNameRole).toString();
    if (sender.isEmpty()) sender = shortName(index.data(TimelineModel::SenderRole).toString());
    QString senderId = index.data(TimelineModel::SenderRole).toString();
    QString avatarUrl = index.data(TimelineModel::AvatarUrlRole).toString();
    QString mxcUrl = index.data(TimelineModel::MxcUrlRole).toString();
    QString mimetype = index.data(TimelineModel::MimetypeRole).toString();
    bool imageLoaded = index.data(TimelineModel::ImageLoadedRole).toBool();
    bool isMovie = index.data(TimelineModel::IsMovieRole).toBool();

    // Draw avatar (left side, 32x32 circle)
    int avatarSize = 32;
    int padding = 8;
    QRect avatarRect(option.rect.x() + padding, option.rect.y() + padding, avatarSize, avatarSize);

    if (type == "m.room.message" || type == "m.room.encrypted") {
        // Try to load the sender's avatar
        QImage avatarImg;
        if (loader_ && !avatarUrl.isEmpty()) {
            if (loader_->hasImage(avatarUrl.toStdString())) {
                avatarImg = loader_->getCached(avatarUrl.toStdString());
            } else {
                // Trigger async load — next paint will pick up cached image
                QString avatarCopy = avatarUrl;
                auto* model = const_cast<QAbstractItemModel*>(index.model());
                const_cast<TimelineDelegate*>(this)->loader_->fetchThumbnail(
                    avatarUrl.toStdString(), 64, 64,
                    [avatarCopy, model](const QImage& img) {
                        if (img.isNull() || !model) return;
                        for (int i = 0; i < model->rowCount(); ++i) {
                            auto idx = model->index(i, 0);
                            if (idx.data(TimelineModel::AvatarUrlRole).toString() == avatarCopy) {
                                emit model->dataChanged(idx, idx);
                                break;
                            }
                        }
                    });
            }
        }
        drawAvatar(painter, avatarRect, senderId, sender, avatarImg);
    }

    // Content area (right of avatar)
    int contentX = avatarRect.right() + padding;
    QRect contentRect(contentX, option.rect.y() + padding,
                      option.rect.width() - contentX - padding,
                      option.rect.height() - 2 * padding);

    // Render text message
    QString html = formatMessageHtml(index);
    renderText(painter, contentRect, html);

    // Render image if applicable
    if ((msgtype == "m.image" || msgtype == "m.video") && !mxcUrl.isEmpty()) {
        if (imageLoaded) {
            QImage img = index.data(TimelineModel::ImageRole).value<QImage>();
            if (!img.isNull()) {
                int imgY = contentRect.y() + 40;
                QRect imgRect(contentX, imgY, qMin(300, img.width()), qMin(200, img.height()));
                painter->drawImage(imgRect, img.scaled(imgRect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
            }
        } else {
            // Trigger async load — when complete, the model's setImage()
            // will emit dataChanged and this row will be repainted.
            // We use a const_cast to call the non-const fetchThumbnail.
            // The callback updates the model via setImage().
            QString eventId = index.data(TimelineModel::EventIdRole).toString();
            const_cast<TimelineDelegate*>(this)->loader_->fetchThumbnail(
                mxcUrl.toStdString(), 300, 200,
                [eventId, model = const_cast<QAbstractItemModel*>(index.model())]
                (const QImage& img) {
                    if (!img.isNull() && model) {
                        // Find the row and update it
                        for (int i = 0; i < model->rowCount(); ++i) {
                            auto idx = model->index(i, 0);
                            if (idx.data(TimelineModel::EventIdRole).toString() == eventId) {
                                // Cast to TimelineModel to call setImage
                                auto* tm = qobject_cast<TimelineModel*>(model);
                                if (tm) {
                                    tm->setImage(eventId.toStdString(), img);
                                }
                                break;
                            }
                        }
                    }
                });
            painter->setPen(QColor("#969696"));
            painter->drawText(contentRect.x(), contentRect.y() + 50, "[loading image...]");
        }
    }

    // Draw reactions (below message) — render emoji pills.
    // We retrieve the reactions as a QStringList via ReactionsRole.
    // The TimelineModel doesn't natively expose them via data(), so we use
    // the TimelineModel::findRow + at() path via a custom ReactionsSummaryRole.
    // For simplicity, we draw reaction pills via the model's data() returning
    // a QStringList of "emoji (count)" entries — registered via Q_DECLARE_METATYPE.
    auto reactionsVar = index.data(TimelineModel::ReactionsRole);
    QStringList reactions = reactionsVar.value<QStringList>();
    if (!reactions.isEmpty()) {
        int rx = contentX;
        int ry = option.rect.y() + option.rect.height() - 24;
        QFont pillFont = painter->font();
        pillFont.setPointSize(9);
        painter->setFont(pillFont);
        QFontMetrics fm(pillFont);
        for (const QString& pill : reactions) {
            int textWidth = fm.horizontalAdvance(pill);
            QRect pillRect(rx, ry, textWidth + 16, 20);
            painter->setPen(QColor("#3a3a3a"));
            painter->setBrush(QColor("#2a2a2a"));
            painter->drawRoundedRect(pillRect, 8, 8);
            painter->setPen(QColor("#e8e8e8"));
            painter->drawText(pillRect, Qt::AlignCenter, pill);
            rx += pillRect.width() + 4;
            if (rx > contentRect.right() - 60) break;  // wrap not implemented — stop
        }
    }

    painter->restore();
}

QSize TimelineDelegate::sizeHint(const QStyleOptionViewItem& option,
                                   const QModelIndex& index) const {
    QString type = index.data(TimelineModel::TypeRole).toString();
    QString msgtype = index.data(TimelineModel::MsgTypeRole).toString();
    QString body = index.data(TimelineModel::BodyRole).toString();
    bool imageLoaded = index.data(TimelineModel::ImageLoadedRole).toBool();

    int width = option.rect.width();
    if (width <= 0) width = 600;

    int height = 50;  // default: avatar + sender + time + one line

    if (type == "m.room.message" && (msgtype == "m.text" || msgtype.isEmpty())) {
        // Estimate text height
        auto doc = std::make_shared<QTextDocument>();
        QString html = formatMessageHtml(index);
        doc->setHtml(html);
        doc->setTextWidth(width - 60);  // minus avatar + padding
        height = static_cast<int>(doc->size().height()) + 20;
    } else if (msgtype == "m.image" || msgtype == "m.video") {
        height = imageLoaded ? 260 : 80;  // image + text, or placeholder
    } else if (type == "m.room.member" || type == "m.room.redaction") {
        height = 30;
    }

    return QSize(width, qMax(height, 40));
}

bool TimelineDelegate::editorEvent(QEvent* event, QAbstractItemModel* model,
                                     const QStyleOptionViewItem& option,
                                     const QModelIndex& index) {
    if (event->type() == QEvent::MouseButtonRelease) {
        auto* me = static_cast<QMouseEvent*>(event);
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

} // namespace progressive::desktop
