// src/ui/room_list_delegate.cpp — paints room list with avatars.
#include "room_list_delegate.hpp"
#include "image_loader.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QImage>
#include <QFont>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QPalette>
#include <QAbstractItemModel>
#include <cstdio>

namespace progressive::desktop {

RoomListDelegate::RoomListDelegate(ImageLoader* loader, QObject* parent)
    : QStyledItemDelegate(parent), loader_(loader) {}

QColor RoomListDelegate::colorFromId(const QString& id) const {
    uint hash = 0;
    for (QChar c : id) hash = hash * 31 + c.unicode();
    int hue = static_cast<int>(hash % 360);
    return QColor::fromHsl(hue, 120, 130);
}

void RoomListDelegate::drawAvatar(QPainter* painter, const QRect& rect,
                                    const QString& roomId, const QString& name,
                                    const QImage& img) const {
    if (!img.isNull()) {
        // Draw image as circle
        QImage scaled = img.scaled(rect.size(), Qt::KeepAspectRatioByExpanding,
                                    Qt::SmoothTransformation);
        // Crop to circle
        QPainterPath path;
        path.addEllipse(rect);
        painter->save();
        painter->setClipPath(path);
        painter->drawImage(rect, scaled);
        painter->restore();
        return;
    }
    // Draw colored circle with first letter
    QColor color = colorFromId(roomId);
    painter->setBrush(color);
    painter->setPen(Qt::NoPen);
    painter->drawEllipse(rect);
    QString letter = name.isEmpty() ? "?" : QString(name[0].toUpper());
    QFont font = painter->font();
    font.setBold(true);
    font.setPointSize(12);
    painter->setFont(font);
    painter->setPen(Qt::white);
    painter->drawText(rect, Qt::AlignCenter, letter);
}

void RoomListDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                const QModelIndex& index) const {
    painter->save();
    bool isInvite = index.data(RoomListModel::IsInviteRole).toBool();
    // Background — invites get an amber tint to stand out
    if (option.state & QStyle::State_Selected) {
        painter->fillRect(option.rect, QColor(50, 80, 130));
    } else if (isInvite) {
        painter->fillRect(option.rect, QColor(40, 30, 20));
    } else {
        painter->fillRect(option.rect, QColor(20, 20, 20));
    }

    QString name = index.data(RoomListModel::NameRole).toString();
    QString lastMsg = index.data(RoomListModel::LastMessageRole).toString();
    QString roomId = index.data(RoomListModel::RoomIdRole).toString();
    QString avatarUrl = index.data(RoomListModel::AvatarUrlRole).toString();
    int unread = index.data(RoomListModel::UnreadRole).toInt();
    bool isEncrypted = index.data(RoomListModel::IsEncryptedRole).toBool();
    auto typingVar = index.data(RoomListModel::TypingUsersRole);
    QStringList typingUsers = typingVar.value<QStringList>();
    bool hasTyping = !typingUsers.isEmpty();

    int padding = 8;
    int avatarSize = 36;
    QRect avatarRect(option.rect.x() + padding,
                     option.rect.y() + (option.rect.height() - avatarSize) / 2,
                     avatarSize, avatarSize);

    // Try to get cached avatar from ImageLoader
    QImage avatarImg;
    if (loader_ && !avatarUrl.isEmpty()) {
        // Use the cached version if available; otherwise trigger async load
        if (loader_->hasImage(avatarUrl.toStdString())) {
            avatarImg = loader_->getCached(avatarUrl.toStdString());
            if (avatarImg.isNull()) {
                std::fprintf(stderr, "[avatar] cached but null for %s\n", roomId.toUtf8().data());
            }
    } else if (hasTyping && !typingUsers.isEmpty()) {
        painter->setPen(QColor("#6c6"));
        // Show first typing user name + "..."
        QString firstName = typingUsers.first();
        if (!firstName.isEmpty() && firstName[0] == '@') {
            auto colon = firstName.indexOf(':');
            if (colon > 1) firstName = firstName.mid(1, colon - 1);
            else firstName = firstName.mid(1);
        }
        int count = typingUsers.size();
        QString hint = (count == 1) ? (firstName + " is typing...")
                     : (count == 2) ? (firstName + " and 1 other are typing...")
                     : (firstName + " and " + QString::number(count - 1) + " others are typing...");
        painter->drawText(msgRect, Qt::AlignLeft | Qt::AlignVCenter,
                          QFontMetrics(msgFont).elidedText(hint, Qt::ElideRight, textWidth));
    } else {
            // Trigger async load — the callback will update the model via
            // dataChanged, causing a repaint.
            QString roomIdCopy = roomId;
            QString avatarUrlCopy = avatarUrl;
            auto* model = const_cast<QAbstractItemModel*>(index.model());
            const_cast<RoomListDelegate*>(this)->loader_->fetchThumbnail(
                avatarUrl.toStdString(), 64, 64,
                [roomIdCopy, avatarUrlCopy, model](const QImage& img) {
                    if (img.isNull() || !model) return;
                    // Trigger dataChanged for the row with this roomId —
                    // next paint will pick up the cached image from ImageLoader.
                    for (int i = 0; i < model->rowCount(); ++i) {
                        auto idx = model->index(i, 0);
                        if (idx.data(RoomListModel::RoomIdRole).toString() == roomIdCopy) {
                            emit model->dataChanged(idx, idx, {RoomListModel::AvatarUrlRole});
                            break;
                        }
                    }
                });
        }
    }

    drawAvatar(painter, avatarRect, roomId, name, avatarImg);

    // Text area (right of avatar)
    int textX = avatarRect.right() + padding;
    int textWidth = option.rect.width() - textX - padding - 30;
    QRect nameRect(textX, option.rect.y() + 6, textWidth, 20);
    QRect msgRect(textX, option.rect.y() + 26, textWidth, 20);

    // Name
    QFont nameFont = painter->font();
    nameFont.setBold(true);
    nameFont.setPointSize(10);
    painter->setFont(nameFont);
    painter->setPen(QColor("#e8e8e8"));
    QString displayName = name;
    if (isInvite) displayName = "✉ " + name;
    if (isEncrypted) displayName = "🔒 " + displayName;
    painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter,
                      QFontMetrics(nameFont).elidedText(displayName, Qt::ElideRight, textWidth));

    // Last message / invite hint
    QFont msgFont = painter->font();
    msgFont.setBold(false);
    msgFont.setPointSize(9);
    painter->setFont(msgFont);
    if (isInvite) {
        painter->setPen(QColor("#ffaa44"));
        QString inviterName = index.data(RoomListModel::InviterRole).toString();
        // Clean up inviter MXID: @bob:matrix.org → bob
        if (!inviterName.isEmpty() && inviterName[0] == '@') {
            auto colon = inviterName.indexOf(':');
            if (colon > 1) inviterName = inviterName.mid(1, colon - 1);
            else inviterName = inviterName.mid(1);
        }
        if (inviterName.isEmpty()) inviterName = "Someone";
        QString hint = inviterName + " invited you";
        painter->drawText(msgRect, Qt::AlignLeft | Qt::AlignVCenter,
                          QFontMetrics(msgFont).elidedText(hint, Qt::ElideRight, textWidth - 60));
        // Draw small [✓] [✗] buttons on the right
        QFont btnFont = painter->font();
        btnFont.setPointSize(11);
        btnFont.setBold(true);
        painter->setFont(btnFont);
        int btnY = option.rect.y() + (option.rect.height() - 22) / 2;
        // Accept (green circle)
        QRect acceptRect(option.rect.right() - 52, btnY, 22, 22);
        painter->setBrush(QColor("#2d6a2d"));
        painter->setPen(Qt::NoPen);
        painter->drawRoundedRect(acceptRect, 4, 4);
        painter->setPen(QColor("#6c6"));
        painter->drawText(acceptRect, Qt::AlignCenter, "✓");
        // Reject (red circle)
        QRect rejectRect(option.rect.right() - 26, btnY, 22, 22);
        painter->setBrush(QColor("#6a2d2d"));
        painter->setPen(Qt::NoPen);
        painter->drawRoundedRect(rejectRect, 4, 4);
        painter->setPen(QColor("#f66"));
        painter->drawText(rejectRect, Qt::AlignCenter, "✗");
    } else {
        painter->setPen(QColor("#969696"));
        painter->drawText(msgRect, Qt::AlignLeft | Qt::AlignVCenter,
                          QFontMetrics(msgFont).elidedText(lastMsg, Qt::ElideRight, textWidth));
    }

    // Unread badge (right side)
    if (unread > 0) {
        QRect badgeRect(option.rect.right() - 28, option.rect.y() + 12, 24, 24);
        painter->setBrush(QColor(50, 130, 220));
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(badgeRect);
        QFont badgeFont = painter->font();
        badgeFont.setBold(true);
        badgeFont.setPointSize(8);
        painter->setFont(badgeFont);
        painter->setPen(Qt::white);
        painter->drawText(badgeRect, Qt::AlignCenter, QString::number(unread));
    }

    painter->restore();
}

QSize RoomListDelegate::sizeHint(const QStyleOptionViewItem& option,
                                   const QModelIndex& index) const {
    return QSize(option.rect.width(), 56);
}

bool RoomListDelegate::editorEvent(QEvent* event, QAbstractItemModel* model,
                                     const QStyleOptionViewItem& option,
                                     const QModelIndex& index) {
    if (event->type() != QEvent::MouseButtonRelease) return false;
    auto* me = static_cast<QMouseEvent*>(event);
    bool isInvite = index.data(RoomListModel::IsInviteRole).toBool();
    if (!isInvite) return false;

    int btnY = option.rect.y() + (option.rect.height() - 22) / 2;
    QRect acceptRect(option.rect.right() - 52, btnY, 22, 22);
    QRect rejectRect(option.rect.right() - 26, btnY, 22, 22);

    if (acceptRect.contains(me->pos())) {
        emit inviteAccepted(index.data(RoomListModel::RoomIdRole).toString());
        return true;
    }
    if (rejectRect.contains(me->pos())) {
        emit inviteRejected(index.data(RoomListModel::RoomIdRole).toString());
        return true;
    }
    return false;
}

} // namespace progressive::desktop
