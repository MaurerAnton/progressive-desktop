// src/ui/theme.hpp — dark theme + design tokens for progressive-desktop.
#pragma once

#include <QColor>

class QApplication;

namespace progressive::desktop {

// Design tokens — single source of truth for all visual constants.
// Used by timeline delegate, room list delegate, and future UI components.
struct Design {
    // Bubble colors
    static inline QColor incomingBubble  = QColor("#2a2a3e");
    static inline QColor outgoingBubble  = QColor("#0f3460");
    static inline QColor textColor       = QColor("#f0f0f0");
    static inline QColor timeColor       = QColor("#aaa");
    static inline QColor systemTextColor = QColor("#777");

    // Backgrounds
    static inline QColor rowBgDark  = QColor(20, 20, 20);
    static inline QColor selectedBg = QColor(50, 80, 130);

    // Reactions
    static inline QColor reactionBg       = QColor("#2a2a2a");
    static inline QColor reactionBorder   = QColor("#3a3a3a");
    static inline QColor linkOnOutgoing   = QColor("#6bb4ff");

    // Accents
    static inline QColor pinnedColor      = QColor("#ffaa00");
    static inline QColor threadColor      = QColor("#6699cc");
    static inline QColor typingColor      = QColor("#6c6");
    static inline QColor emoteColor       = QColor("#c0c0c0");
    static inline QColor inviteRowBg      = QColor(40, 30, 20);
    static inline QColor inviteTextColor  = QColor("#ffaa44");
    static inline QColor viewBg           = QColor(0x1e, 0x1e, 0x1e);
    static inline QColor rowBgNormal      = QColor(0x1e, 0x1e, 0x1e);
    static inline QColor unreadBadgeColor = QColor(50, 130, 220);

    // Timestamp + muted text
    static inline QColor mutedTextColor   = QColor("#888");
    static inline QColor dimTextColor     = QColor("#969696");
    static inline QColor replyLineColor   = QColor("#555");
    static inline QColor reactionTextColor = QColor("#e8e8e8");

    // File card
    static inline QColor fileCardBg       = QColor("#1e1e2e");
    static inline QColor fileCardBorder   = QColor("#444");
    static inline QColor fileCardIconText = QColor("#ccc");
    static inline QColor fileCardFileName = QColor("#ddd");
    static inline QColor fileAudioBar     = QColor("#4a6");
    static inline QColor fileFileBar      = QColor("#48a");

    // Image placeholder
    static inline QColor imgPlaceholderBg   = QColor("#1a1a1a");
    static inline QColor imgPlaceholderText = QColor("#888");

    // Invite buttons
    static inline QColor acceptButtonBg = QColor("#2d6a2d");
    static inline QColor rejectButtonBg = QColor("#6a2d2d");
    static inline QColor rejectButtonText = QColor("#f66");

    // Emoji picker
    static inline QColor emojiHoverBg = QColor("#3a3a3a");

    // Avatar
    static constexpr int avatarSize    = 36;
    static constexpr int bubbleRadius  = 12;
    static constexpr int bubblePadding = 10;
    static constexpr int margin        = 8;
    static constexpr int gap           = 8;
    static inline double fontScale      = 1.0;
};

// Apply the default dark theme. Call once after QApplication is constructed.
void applyDarkTheme(QApplication& app);

// Deterministic color from user ID — unified across all delegates.
inline QColor colorFromId(const QString& id) {
    uint hash = 0;
    for (QChar c : id) hash = hash * 31 + c.unicode();
    return QColor::fromHsl(static_cast<int>(hash % 360), 180, 140);
}

} // namespace progressive::desktop
