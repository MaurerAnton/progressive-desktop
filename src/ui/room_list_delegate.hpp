// src/ui/room_list_delegate.hpp — paints room list rows with avatar + name.
#pragma once
#include <QStyledItemDelegate>
#include "room_list_model.hpp"

class QPainter;
class QModelIndex;

namespace progressive::desktop {

class ImageLoader;

class RoomListDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit RoomListDelegate(ImageLoader* loader, QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;
    bool editorEvent(QEvent* event, QAbstractItemModel* model,
                     const QStyleOptionViewItem& option,
                     const QModelIndex& index) override;

signals:
    void inviteAccepted(const QString& roomId);
    void inviteRejected(const QString& roomId);

private:
    ImageLoader* loader_;
    void drawAvatar(QPainter* painter, const QRect& rect,
                    const QString& roomId, const QString& name,
                    const QImage& img) const;
};

} // namespace progressive::desktop
