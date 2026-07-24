// src/ui/timeline/timeline_layout.hpp — bubble layout computation.
#pragma once
#include <QFontMetrics>
#include <QModelIndex>
#include <QRect>
#include <QString>

namespace progressive::desktop::timeline_layout {

inline constexpr int kAvatarSize    = 36;
inline constexpr int kMargin        = 8;
inline constexpr int kGap           = 8;
inline constexpr int kBubbleRadius  = 12;
inline constexpr int kBubblePadding = 10;
inline constexpr int kPadTop        = 6;
inline constexpr int kPadBottom     = 4;
inline constexpr int kMaxBubbleW    = 480;
inline constexpr int kSameSenderGap = 2;
inline constexpr int kTimeRowH      = 14;

inline constexpr int kMaxImageW        = 300;
inline constexpr int kImageLoadedH     = 200;
inline constexpr int kImagePlaceholderH = 100;

struct BubbleLayout {
    int nameH        = 0;
    int textH        = 0;
    int imageH       = 0;
    int pinnedH      = 0;
    int threadReplyH = 0;
    int replyH       = 0;
    int threadCountH = 0;
    int reactionH    = 0;
    int bubbleH      = 0;
    bool isFirstInGroup = true;
    bool isLastInGroup  = true;
    bool isEmote     = false;

    int totalBubbleH() const;
    int totalHeight() const;
};

int ds(double pt);
int calcTextHeight(const QString& body, const QString& msgtype, int textWidth);
BubbleLayout computeLayout(const QModelIndex& idx, const QString& myUserId, int bubbleW);

} // namespace progressive::desktop::timeline_layout
