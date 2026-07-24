// src/ui/timeline_delegate.cpp — chat bubbles with avatars.
#include "timeline_delegate.hpp"
#include "timeline_model.hpp"
#include "timeline_layout.hpp"
#include "timeline_painter.hpp"
#include "../shared/image_loader.hpp"
#include "../shared/theme.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QTextDocument>
#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QDateTime>
#include <QMouseEvent>
#include <QUrl>
#include <QRegularExpression>
#include <QGuiApplication>
#include <progressive/markdown.hpp>

using namespace progressive::desktop::timeline_layout;

namespace progressive::desktop {

TimelineDelegate::TimelineDelegate(ImageLoader* loader, QObject* parent)
    : QStyledItemDelegate(parent), loader_(loader) {}

void TimelineDelegate::paint(QPainter* p, const QStyleOptionViewItem& opt,
                               const QModelIndex& idx) const {
    p->save();
    p->setRenderHint(QPainter::Antialiasing);

    if (opt.state & QStyle::State_Selected) {
        p->fillRect(opt.rect, Design::selectedBg);
    } else {
        p->fillRect(opt.rect, Design::viewBg);
    }

    QString type = idx.data(TimelineModel::TypeRole).toString();
    if (type == "progressive.system") {
        timeline_render::drawSystemRow(p, opt.rect, idx, type);
    } else {
        int width = opt.rect.width();
        if (width <= 40) width = 600;
        int bubbleW = qMin(kMaxBubbleW, width - kAvatarSize - kGap - kMargin * 2);
        BubbleLayout L = computeLayout(idx, myUserId_, bubbleW);
        timeline_render::drawMessageBubble(p, opt.rect, idx, L, myUserId_,
                                           loader_, pendingFetches_);
    }

    p->restore();
}

QSize TimelineDelegate::sizeHint(const QStyleOptionViewItem& opt,
                                   const QModelIndex& idx) const {
    QString type = idx.data(TimelineModel::TypeRole).toString();
    if (type == "progressive.system") {
        return QSize(opt.rect.width() > 40 ? opt.rect.width() : 600, 28);
    }

    int width = opt.rect.width();
    if (width <= 40) width = 600;
    int bubbleW = qMin(kMaxBubbleW, width - kAvatarSize - kGap - kMargin * 2);

    BubbleLayout L = computeLayout(idx, myUserId_, bubbleW);

    int totalH = L.totalHeight();
    return QSize(width, qMax(totalH, kAvatarSize + 8));
}

bool TimelineDelegate::editorEvent(QEvent* event, QAbstractItemModel* model,
                                     const QStyleOptionViewItem& option,
                                     const QModelIndex& index) {
    if (event->type() != QEvent::MouseButtonRelease)
        return QStyledItemDelegate::editorEvent(event, model, option, index);

    auto* me = static_cast<QMouseEvent*>(event);
    QString eventId = index.data(TimelineModel::EventIdRole).toString();
    QString mxcUrl = index.data(TimelineModel::MxcUrlRole).toString();
    QString msgtype = index.data(TimelineModel::MsgTypeRole).toString();
    QString body = index.data(TimelineModel::BodyRole).toString();
    QString type = index.data(TimelineModel::TypeRole).toString();
    if (type == "progressive.system") return false;

    int width = option.rect.width();
    int bubbleW = qMin(kMaxBubbleW, width - kAvatarSize - kGap - kMargin * 2);
    BubbleLayout L = computeLayout(index, myUserId_, bubbleW);
    QString senderId = index.data(TimelineModel::SenderRole).toString();
    bool isOutgoing = !myUserId_.isEmpty() && senderId == myUserId_;
    int bubbleX = isOutgoing ? (option.rect.right() - kMargin - kAvatarSize - kGap - bubbleW)
                              : (option.rect.x() + kMargin + kAvatarSize + kGap);
    int topY = option.rect.y() + (L.isFirstInGroup ? 4 : kSameSenderGap);
    int bubbleY = topY + L.nameH;

    auto rxns = index.data(TimelineModel::ReactionsRole).value<QStringList>();
    if (!rxns.isEmpty()) {
        int baseY = L.isLastInGroup ? (bubbleY + L.bubbleH + 2)
                                     : (bubbleY + L.bubbleH - kPadBottom - kTimeRowH - L.reactionH);
        int rx = bubbleX + kBubblePadding, ry = baseY;
        QFont pillFont; pillFont.setPointSize(ds(9));
        QFontMetrics fm(pillFont);
        int maxX = bubbleX + bubbleW - kBubblePadding;
        int shown = 0, rowNum = 0;
        for (int i = 0; i < rxns.size(); ++i) {
            int pw = fm.horizontalAdvance(rxns[i]) + 16;
            if (rx + pw > maxX && shown > 0) { rx = bubbleX + kBubblePadding; ry += 20; rowNum++; shown = 0; if (rowNum >= 2) break; }
            if (rx + pw > maxX && shown == 0) pw = maxX - rx;
            QRect pr(rx, ry, pw, 20);
            if (pr.contains(me->pos())) {
                emit reactionClicked(eventId, rxns[i]);
                return true;
            }
            rx += pw + 3; shown++;
        }
    }

    bool isReply = index.data(TimelineModel::IsReplyRole).toBool();
    if (isReply && L.replyH > 0) {
        int replyY = bubbleY + kPadTop;
        if (L.pinnedH) replyY += 14;
        if (L.threadReplyH) replyY += 14;
        QRect replyZone(bubbleX + kBubblePadding, replyY, bubbleW - kBubblePadding*2, 14);
        if (replyZone.contains(me->pos())) {
            emit messageClicked(index.data(TimelineModel::ReplyToRole).toString());
            return true;
        }
    }

    int threadCount = index.data(TimelineModel::ThreadCountRole).toInt();
    if (threadCount > 0) {
        int tcY = bubbleY + L.bubbleH - kPadBottom - kTimeRowH - L.threadCountH - 2;
        int textX = bubbleX + kBubblePadding;
        int textW = bubbleW - kBubblePadding * 2;
        QRect threadZone(textX, tcY, textW, 14);
        if (threadZone.contains(me->pos())) {
            emit threadIndicatorClicked(eventId);
            return true;
        }
    }

    if ((msgtype == "m.image" || msgtype == "m.video") && !mxcUrl.isEmpty() && L.imageH > 0) {
        int imgY = bubbleY + kPadTop;
        if (L.pinnedH) imgY += 14;
        if (L.threadReplyH) imgY += 14;
        if (L.replyH) imgY += 14;
        if (L.textH > 0) imgY += L.textH + 4;
        QRect imgZone(bubbleX + kBubblePadding, imgY, qMin(bubbleW - kBubblePadding*2, kMaxImageW), L.imageH);
        if (imgZone.contains(me->pos())) {
            emit imageClicked(eventId, mxcUrl);
            return true;
        }
    }

    if (msgtype == "m.text" || msgtype.isEmpty()) {
        QTextDocument doc;
        doc.setDefaultFont(QFont(QApplication::font().family(), ds(10)));
        std::string html = progressive::markdownToHtml(body.toStdString());
        doc.setHtml(html.empty() ? body.toHtmlEscaped() : QString::fromStdString(html));
        doc.setTextWidth(bubbleW - kBubblePadding * 2);
        int textY = bubbleY + kPadTop;
        if (L.pinnedH) textY += 14;
        if (L.threadReplyH) textY += 14;
        if (L.replyH) textY += 14;
        QPointF docPos(me->pos().x() - bubbleX - kBubblePadding, me->pos().y() - textY);
        QString anchor = doc.documentLayout()->anchorAt(docPos);
        if (!anchor.isEmpty() && (anchor.startsWith("https://") || anchor.startsWith("http://"))) {
            emit linkClicked(anchor);
            return true;
        }
        if (!anchor.isEmpty() && anchor.startsWith("matrix.to")) {
            emit linkClicked("https://" + anchor);
            return true;
        }
    }

    emit messageClicked(eventId);
    return true;
}

} // namespace progressive::desktop
