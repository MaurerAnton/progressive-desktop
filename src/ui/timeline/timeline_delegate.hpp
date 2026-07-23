// src/ui/timeline_delegate.hpp — paints timeline events.
#pragma once
#include <QStyledItemDelegate>
#include <QHash>
#include <unordered_set>
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
    void threadIndicatorClicked(const QString& eventId);

private:
    ImageLoader* loader_;
    QString myUserId_;
    mutable std::unordered_set<std::string> pendingFetches_;
};

} // namespace progressive::desktop
