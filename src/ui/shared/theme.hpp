// src/ui/theme.hpp — dark theme + design tokens for progressive-desktop.
#pragma once

#include <QColor>
#include <vector>

class QApplication;

namespace progressive::desktop {

// Design tokens — single source of truth for all visual constants.
// Used by timeline delegate, room list delegate, and future UI components.
struct Design {
    // Backgrounds
    static inline QColor viewBg          = QColor(0x1e, 0x1e, 0x1e);
    static inline QColor selectedBg      = QColor(50, 80, 130);
    static inline QColor inviteRowBg     = QColor(40, 30, 20);
    static inline QColor inputBg         = QColor(20, 20, 20);    // #141414

    // Bubble colors
    static inline QColor incomingBubble  = QColor("#2a2a3e");
    static inline QColor outgoingBubble  = QColor("#0f3460");

    // Text
    static inline QColor textColor       = QColor("#f0f0f0");
    static inline QColor timeColor       = QColor("#aaa");
    static inline QColor systemTextColor = QColor("#777");
    static inline QColor mutedTextColor  = QColor("#888");
    static inline QColor dimTextColor    = QColor("#969696");
    static inline QColor reactionTextColor = QColor("#e8e8e8");
    static inline QColor inviteTextColor = QColor("#ffaa44");
    static inline QColor deletedTextColor = QColor("#666");

    // Accents
    static inline QColor accentColor     = QColor("#2a82da");    // Matrix blue
    static inline QColor pinnedColor     = QColor("#ffaa00");
    static inline QColor threadColor     = QColor("#6699cc");
    static inline QColor typingColor     = QColor("#6c6");
    static inline QColor emoteColor      = QColor("#c0c0c0");
    static inline QColor linkOnOutgoing  = QColor("#6bb4ff");
    static inline QColor unreadBadgeColor = QColor(50, 130, 220);
    static inline QColor playBtnOverlay  = QColor(255, 255, 255, 80);

    // Status / semantic
    static inline QColor dangerText      = QColor("#f66");
    static inline QColor dangerBg        = QColor("#6a2d2d");
    static inline QColor acceptBg        = QColor("#2d6a2d");

    // Borders
    static inline QColor borderColor     = QColor("#3a3a3a");
    static inline QColor hoverBorder     = QColor("#4a4a4a");
    static inline QColor replyLineColor  = QColor("#555");

    // File card
    static inline QColor fileCardBg       = QColor("#1e1e2e");
    static inline QColor fileCardBorder   = QColor("#444");
    static inline QColor fileCardIconText = QColor("#ccc");
    static inline QColor fileCardFileName = QColor("#ddd");
    static inline QColor fileAudioBar     = QColor("#4a6");
    static inline QColor fileFileBar      = QColor("#48a");

    // Image placeholder
    static inline QColor imgPlaceholderBg = QColor("#1a1a1a");

    // Tray icon
    static inline QColor trayIconBg   = QColor("#1a1a2e");

    // Dimensions
    static constexpr int avatarSize    = 36;
    static constexpr int bubbleRadius  = 12;
    static constexpr int bubblePadding = 10;
    static constexpr int margin        = 8;
    static constexpr int gap           = 8;
    static inline double fontScale      = 1.0;
};

// Apply the default dark theme. Call once after QApplication is constructed.
void applyDarkTheme(QApplication& app);

// Load/save Design tokens from QSettings under theme/ keys.
// Called at startup (load, before applyDarkTheme) and on picker save.
struct Theme {
    static void load();     // read QSettings → Design:: tokens
    static void save();     // write Design:: tokens → QSettings
    static void reapply();  // re-apply palette + stylesheet after token changes

    struct TokenEntry { const char* qkey; QColor* field; };
    static const std::vector<TokenEntry>& tokenRegistry();
};

// Deterministic color from user ID — unified across all delegates.
inline QColor colorFromId(const QString& id) {
    uint hash = 0;
    for (QChar c : id) hash = hash * 31 + c.unicode();
    return QColor::fromHsl(static_cast<int>(hash % 360), 180, 140);
}

} // namespace progressive::desktop
