# docs/design.md — Design Tokens (single source of truth)

> Written Aug 2, 2026 so a future GTK frontend (progressive-gui-gtk) can match the Qt UI
> without reading code. Source of truth is the code: `src/ui/shared/theme.hpp` (colors +
> dimensions) and `src/ui/timeline/timeline_layout.hpp` (layout constants). If code changes,
> update this doc. Dark theme is current; light theme planned (auto = follow system).

## Colors (from `Design` struct in `src/ui/shared/theme.hpp`)

### Backgrounds
| Token | Value | Use |
|---|---|---|
| viewBg | `#1e1e1e` | main window background |
| selectedBg | `rgb(50,80,130)` | selected room / row |
| inviteRowBg | `rgb(40,30,20)` | invitation row |
| inputBg | `#141414` | message input background |

### Bubbles
| Token | Value | Use |
|---|---|---|
| incomingBubble | `#2a2a3e` | incoming message bubble |
| outgoingBubble | `#0f3460` | outgoing message bubble |

### Text
| Token | Value | Use |
|---|---|---|
| textColor | `#f0f0f0` | primary text |
| timeColor | `#aaa` | timestamp |
| systemTextColor | `#777` | system events (join/leave) |
| mutedTextColor | `#888` | muted/secondary |
| dimTextColor | `#969696` | dimmed |
| reactionTextColor | `#e8e8e8` | reaction pill text |
| inviteTextColor | `#ffaa44` | invitation accent |
| deletedTextColor | `#666` | redacted/deleted message |

### Accents & semantic
| Token | Value | Use |
|---|---|---|
| accentColor | `#2a82da` | Matrix blue — links, buttons, focus |
| pinnedColor | `#ffaa00` | pinned messages |
| threadColor | `#6699cc` | thread indicators |
| typingColor | `#6c6` | typing indicator |
| emoteColor | `#c0c0c0` | /me emotes |
| linkOnOutgoing | `#6bb4ff` | links inside outgoing bubbles |
| unreadBadgeColor | `rgb(50,130,220)` | unread badge |
| playBtnOverlay | `rgba(255,255,255,80)` | media play button overlay |
| dangerText | `#f66` | errors |
| dangerBg | `#6a2d2d` | destructive action bg |
| acceptBg | `#2d6a2d` | accept action bg |

### Reactions
| Token | Value | Use |
|---|---|---|
| reactionBg | `#2a2a2a` | reaction pill background |

### Borders
| Token | Value | Use |
|---|---|---|
| borderColor | `#3a3a3a` | default border |
| hoverBorder | `#4a4a4a` | hover border |
| replyLineColor | `#555` | reply quote line |

### File card
| Token | Value | Use |
|---|---|---|
| fileCardBg | `#1e1e2e` | file card background |
| fileCardBorder | `#444` | file card border |
| fileCardIconText | `#ccc` | file type icon |
| fileCardFileName | `#ddd` | filename |
| fileAudioBar | `#4a6` | audio progress bar |
| fileFileBar | `#48a` | file progress bar |

### Network log / debug
| Token | Value | Use |
|---|---|---|
| logViewBg | `#0d0d0d` | log panel bg |
| logViewText | `#dddddd` | log text |
| httpGetColor | `#66aaff` | GET |
| httpPostColor | `#66ff66` | POST |
| httpPutColor | `#ffaa66` | PUT |
| httpErrorColor | `#ff6666` | HTTP error |
| http2xxColor | `#66cc66` | 2xx status |
| httpOtherStatusColor | `#ffcc66` | other status |
| httpOtherMethodColor | `#cccccc` | other method |

### Misc
| Token | Value | Use |
|---|---|---|
| accountComboBg | `#1a1a1a` | account switcher bg |
| accountComboText | `#cccccc` | account switcher text |
| imgPlaceholderBg | `#1a1a1a` | image placeholder |
| trayIconBg | `#1a1a2e` | tray icon |

### Avatar color from user ID
Not a fixed palette — deterministic HSL from the ID: `hue = hash(id) % 360`, `sat=180`, `light=140` (`colorFromId` in theme.hpp).

## Dimensions

### Chat / timeline (`timeline_layout.hpp`)
| Token | Value | Use |
|---|---|---|
| avatarSize | 36 px | avatar |
| bubbleRadius | 12 px | bubble corner radius |
| bubblePadding | 10 px | text padding in bubble |
| margin | 8 px | outer margin |
| gap | 8 px | gap between bubbles |
| padTop / padBottom | 6 / 4 px | vertical bubble padding |
| maxBubbleW | 480 px | max bubble width |
| sameSenderGap | 2 px | gap between consecutive same-sender messages |
| timeRowH | 14 px | timestamp row height |
| maxImageW | 300 px | max inline image width |
| imageLoadedH | 200 px | loaded image height |
| imagePlaceholderH | 100 px | image placeholder height |
| fontScale | 1.0 (double) | global font scale factor |
| maxBubbleW | 480 px | max bubble width |

### Font sizes (points, `timeline_layout.hpp`)
| Token | Value |
|---|---|
| body | 10 |
| small | 9 |
| caption | 8 |
| name | 11 |
| icon | 12 |
| emoji | 14 |

### Reactions
| Token | Value |
|---|---|
| pillPad | 16 px horizontal padding |
| pillH | 20 px |
| pillGap | 3 px |
| maxRows | 2 |

### File card (`timeline_layout.hpp`)
Card 38 px high, max 250 px wide; icon 24×30 at (12,4); text at x=40; type label below; 3 px progress bar.

## Components (rules built from tokens)

- **Incoming message**: left-aligned, avatar left, `incomingBubble`.
- **Outgoing message**: right-aligned, avatar right, `outgoingBubble`.
- **System event** (join/leave/name/topic): centered, gray italic (`systemTextColor`), no bubble.
- **Grouped bubbles**: first = full top radius (12) + 0 bottom; middle = all 0; last = 0 top + full bottom (12).
- **Reaction pill**: `reactionBg`, 16 px horizontal padding, 20 px tall, up to 2 rows.
- **Unread badge**: `unreadBadgeColor`, on room list.
- **Context-menu gating**: actions disabled (not hidden) when not applicable (pin/unpin, edit, reply).

## Patterns

- Layout: room list left · timeline center · input bottom (Telegram Desktop reference).
- Keyboard-first: Ctrl+K room switcher, Alt+↑/↓ rooms, Ctrl+↑/↓ messages, Ctrl+R reply, Esc close, Ctrl+Tab accounts.
- Animations: 200 ms ease-out.
- Theme: dark (current) → light (planned) → auto-follow-system (Settings toggle).
