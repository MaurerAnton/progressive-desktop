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
    // Background
    if (option.state & QStyle::State_Selected) {
        painter->fillRect(option.rect, QColor(50, 80, 130));
    } else if (option.features & QStyleOptionViewItem::Alternate) {
        painter->fillRect(option.rect, QColor(26, 26, 26));
    } else {
        painter->fillRect(option.rect, QColor(20, 20, 20));
    }

    QString name = index.data(RoomListModel::NameRole).toString();
    QString lastMsg = index.data(RoomListModel::LastMessageRole).toString();
    QString roomId = index.data(RoomListModel::RoomIdRole).toString();
    QString avatarUrl = index.data(RoomListModel::AvatarUrlRole).toString();
    bool isInvite = index.data(RoomListModel::IsInviteRole).toBool();
    int unread = index.data(RoomListModel::UnreadRole).toInt();
    bool isEncrypted = index.data(RoomListModel::IsEncryptedRole).toBool();

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

    // Last message
    QFont msgFont = painter->font();
    msgFont.setBold(false);
    msgFont.setPointSize(9);
    painter->setFont(msgFont);
    painter->setPen(QColor("#969696"));
    painter->drawText(msgRect, Qt::AlignLeft | Qt::AlignVCenter,
                      QFontMetrics(msgFont).elidedText(lastMsg, Qt::ElideRight, textWidth));

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

} // namespace progressive::desktop
