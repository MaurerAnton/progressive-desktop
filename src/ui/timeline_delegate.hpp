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
    // Cache QTextDocument per event_id for text rendering.
    // Key = event_id hash, value = QTextDocument.
    mutable QHash<QString, std::shared_ptr<QTextDocument>> docCache_;

    void renderText(QPainter* painter, const QRect& rect, const QString& html) const;
    QString formatMessageHtml(const QModelIndex& index) const;
    QColor avatarColor(const QString& userId) const;
    void drawAvatar(QPainter* painter, const QRect& rect, const QString& userId,
                    const QString& name, const QImage& avatarImg = QImage()) const;
};

} // namespace progressive::desktop
