// src/ui/timeline_delegate.hpp — paints timeline events.
#pragma once
#include <QStyledItemDelegate>
#include <QHash>
#include <memory>

class QTextDocument;
class QMovie;

namespace progressive::desktop {

class ImageLoader;

class TimelineDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit TimelineDelegate(ImageLoader* loader, QObject* parent = nullptr);

    void setMyUserId(const std::string& id) { myUserId_ = QString::fromStdString(id); }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;
    bool editorEvent(QEvent* event, QAbstractItemModel* model,
                     const QStyleOptionViewItem& option,
                     const QModelIndex& index) override;

signals:
    void imageClicked(const QString& eventId, const QString& mxcUrl);
    void messageClicked(const QString& eventId);
    void linkClicked(const QString& url);
    void reactionClicked(const QString& eventId, const QString& emoji);

private:
    ImageLoader* loader_;
    QString myUserId_;
    // Per-row cached text height for faster sizeHint
    mutable QHash<QString, int> heightCache_;

    void drawBubbleAvatar(QPainter* p, int x, int y, const QModelIndex& idx,
                          const QString& senderId, const QString& senderName,
                          const QString& avatarUrl) const;
    void drawBubble(QPainter* p, int x, int y, int w, int h, const QColor& color) const;
    void drawSystemRow(QPainter* p, const QRect& rect, const QModelIndex& idx,
                       const QString& type) const;
    void drawMessageBubble(QPainter* p, const QRect& rowRect,
                           const QModelIndex& idx) const;
    QColor avatarColor(const QString& userId) const;
};

} // namespace progressive::desktop
