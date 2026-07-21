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
    static inline QColor timeColor       = QColor("#888");
    static inline QColor systemTextColor = QColor("#777");

    // Backgrounds
    static inline QColor rowBgDark  = QColor(20, 20, 20);
    static inline QColor rowBgLight = QColor(30, 30, 30);
    static inline QColor selectedBg = QColor(50, 80, 130);

    // Reactions
    static inline QColor reactionBg    = QColor("#2a2a2a");
    static inline QColor reactionBorder = QColor("#3a3a3a");

    // Avatar
    static constexpr int avatarSize    = 36;
    static constexpr int bubbleRadius  = 12;
    static constexpr int bubblePadding = 10;
    static constexpr int margin        = 8;
    static constexpr int gap           = 8;
};

// Apply the default dark theme. Call once after QApplication is constructed.
void applyDarkTheme(QApplication& app);

} // namespace progressive::desktop
