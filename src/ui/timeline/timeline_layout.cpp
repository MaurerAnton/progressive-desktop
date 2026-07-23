// src/ui/timeline/timeline_layout.cpp — bubble layout computation.
#include "timeline_layout.hpp"
#include "timeline_model.hpp"
#include "../shared/theme.hpp"

#include <QApplication>
#include <QFont>
#include <QTextDocument>
#include <progressive/markdown.hpp>

namespace progressive::desktop::timeline_layout {

int ds(double pt) { return qMax(1, (int)(pt * Design::fontScale)); }

int calcTextHeight(const QString& body, const QString& msgtype, int textWidth) {
    if (body.isEmpty()) return 0;
    QTextDocument doc;
    doc.setDefaultFont(QFont(QApplication::font().family(), ds(10)));
    if (msgtype == "m.text" || msgtype == "m.emote" || msgtype.isEmpty()) {
        std::string html = progressive::markdownToHtml(body.toStdString());
        if (html.empty()) doc.setPlainText(body);
        else doc.setHtml(QString::fromStdString(html));
    } else {
        doc.setPlainText(body);
    }
    doc.setTextWidth(textWidth);
    return static_cast<int>(doc.size().height());
}

int BubbleLayout::totalBubbleH() const {
    int h = nameH + kPadTop;
    if (pinnedH)     h += pinnedH;
    if (threadReplyH) h += threadReplyH;
    if (replyH)       h += replyH;
    if (textH > 0)   h += textH + 4;
    if (imageH > 0)  h += imageH + 4;
    h += kPadBottom + kTimeRowH;
    if (threadCountH) h += threadCountH;
    if (reactionH && !isLastInGroup) h += reactionH;
    return h;
}

int BubbleLayout::totalHeight() const {
    int h = (isFirstInGroup ? 4 : kSameSenderGap);
    h += bubbleH;
    h += nameH;
    if (isLastInGroup && reactionH) h += reactionH;
    h += 2;
    return h;
}

BubbleLayout computeLayout(const QModelIndex& idx, const QString& myUserId, int bubbleW) {
    BubbleLayout L;
    QString senderId = idx.data(TimelineModel::SenderRole).toString();
    QString senderName = idx.data(TimelineModel::SenderNameRole).toString();
    QString body = idx.data(TimelineModel::BodyRole).toString();
    QString msgtype = idx.data(TimelineModel::MsgTypeRole).toString();
    QString mxcUrl = idx.data(TimelineModel::MxcUrlRole).toString();
    bool imageLoaded = idx.data(TimelineModel::ImageLoadedRole).toBool();
    int threadCount = idx.data(TimelineModel::ThreadCountRole).toInt();
    bool isThreadReply = idx.data(TimelineModel::IsThreadReplyRole).toBool();
    bool pinned = idx.data(TimelineModel::IsPinnedRole).toBool();
    bool isReply = idx.data(TimelineModel::IsReplyRole).toBool();
    QString replyTo = idx.data(TimelineModel::ReplyToRole).toString();
    bool isOutgoing = !myUserId.isEmpty() && senderId == myUserId;
    bool isEmote = (msgtype == "m.emote");
    bool hasImage = (msgtype == "m.image" || msgtype == "m.video") && !mxcUrl.isEmpty();
    L.isEmote = isEmote;

    bool groupFirst = true, groupLast = true;
    int row = idx.row();
    auto* tm = static_cast<const TimelineModel*>(idx.model());
    if (tm && row >= 0 && row < tm->rowCount(QModelIndex())) {
        auto* evt = tm->at(row);
        if (evt) { groupFirst = evt->groupFirst; groupLast = evt->groupLast; }
    }
    L.isFirstInGroup = groupFirst || isEmote;
    L.isLastInGroup = groupLast || isEmote;

    if (!isOutgoing && !isEmote && L.isFirstInGroup && !senderName.isEmpty())
        L.nameH = 18;

    int textW = bubbleW - kBubblePadding * 2;
    if (!body.isEmpty() || isEmote) {
        QString text = isEmote ? ("* " + senderName + " " + body) : body;
        L.textH = calcTextHeight(text, isEmote ? "m.text" : msgtype, textW);
    }

    if (hasImage)
        L.imageH = imageLoaded ? 200 : 100;

    if (pinned)           L.pinnedH     = 14;
    if (isThreadReply)     L.threadReplyH = 14;
    if (isReply && !replyTo.isEmpty()) L.replyH = 14;
    if (threadCount > 0)  L.threadCountH = 16;

    auto rxns = idx.data(TimelineModel::ReactionsRole).value<QStringList>();
    if (!rxns.isEmpty()) {
        int perRow = qMax(1, (bubbleW - kBubblePadding * 2) / 100);
        L.reactionH = qMin(2, ((int)rxns.size() + perRow - 1) / perRow) * 20;
    }

    L.bubbleH = L.totalBubbleH();
    return L;
}

} // namespace progressive::desktop::timeline_layout
