# docs/client-comparison.md — Matrix Client Feature Comparison

> Research: July 27, 2026. Shows where Progressive Chat stands vs. other Matrix clients.
> Sources: GitHub READMEs, changelogs, Matrix v1.19 spec, explore agent research.

---

## Feature Comparison

| Feature | Progressive Chat (Jul 27) | Nheko 0.12 | FluffyChat 2.4 | Element Desktop | Cinny 4.5 |
|---|---|---|---|---|---|
| **Platform** | Qt6 Widgets (C++20) | Qt6 (C++17) | Flutter | Web + Electron | Web (React) |
| **Target** | PineTab 2 / Desktop | Desktop (Linux/Mac/Win) | Mobile + Desktop | Desktop | Web |
| **E2EE text** | ✅ Phase 1 done | ✅ Since v0.6 | ✅ | ✅ | ✅ |
| **E2EE files** | ❌ DEBT chat_view.cpp:279 | ✅ | ✅ | ✅ | ❌ |
| **E2EE verification (SAS)** | ❌ | ✅ | ✅ | ✅ | ❌ |
| **Cross-signing** | ❌ | ✅ | ✅ | ✅ | ❌ |
| **Key backup (SSSS)** | ❌ | ✅ | ✅ | ✅ | ❌ |
| **Outbound session persistence** | ❌ Lost on restart | ✅ | ✅ | ✅ | — |
| **Threads** | ⚠️ Bugs (fixing) | ✅ | ✅ | ✅ | ❌ |
| **Polls** | ❌ (beta) | ❌ | ✅ | ✅ | ❌ |
| **Ctrl+K switcher** | ❌ | ✅ | — | ✅ | ❌ |
| **Keyboard shortcuts** | F11/F12 only | Full set | Removed | Full set + overlay | None |
| **Custom themes** | ❌ (hardcoded) | Light/Dark/System | Material You | JSON token manifest | 3 named variants |
| **Per-color override** | ❌ | ❌ | ❌ | Via CSS tokens | ❌ |
| **Markdown tables** | ❌ (needs cmark-gfm) | ✅ | ✅ | ✅ | ✅ |
| **Spaces** | ❌ | ✅ | ✅ | ✅ | ❌ |
| **Image/file upload** | ✅ (no E2EE) | ✅ | ✅ | ✅ | — |
| **Image viewer (zoom)** | ✅ | — | ✅ | ✅ | — |
| **Voice messages** | ❌ | ❌ (removed) | ✅ | ✅ | — |
| **Voice/video calls** | ❌ | ✅ | ✅ | ✅ | ❌ |
| **Multi-account** | ⚠️ No UI | ❌ | ❌ | ❌ | ❌ |
| **Server notices** | ❌ | ✅ | — | ✅ | — |
| **Room creation** | DM only | ✅ | ✅ | ✅ | ✅ |
| **Read receipts** | ⚠️ Only send (receive broken) | ✅ | ✅ | ✅ | ✅ |
| **Typing indicators** | ⚠️ Receive only | ✅ | ✅ | ✅ | ✅ |
| **Event source viewer** | ❌ | — | ✅ | ✅ | — |
| **Copy messages** | ❌ | ✅ | ✅ | ✅ | ✅ |
| **Forget room API** | ✅ (b49d554) | ✅ | ✅ | ✅ | ✅ |
| **PineTab optimized** | ✅ (ARM64, low RAM) | ❌ | ❌ | ❌ | ❌ |
| **Binary size** | ~2.8 MB | ~15 MB | ~30 MB | ~200 MB (Electron) | — |
| **RAM (8 rooms)** | 120-150 MB | ~200 MB | ~300 MB | ~500 MB | — |

---

## Nheko Alpha vs Current Comparison

Nheko's alpha (~v0.6, 2019) had:
- Text E2EE
- Basic room management
- Light/dark themes
- Inline images
- Replies
- Read receipts

Nheko current (v0.12.1, Aug 2025) added:
- VoIP (voice + video + screenshare)
- Threads
- Spaces/communities
- Search in timeline
- Push rules
- Pinned messages
- Intentional mentions (MSC4142)
- Event expiration
- Ignoring users
- D-Bus API
- Custom sticker/emote packs
- jdenticon avatars

**Progressive Chat vs Nheko alpha:** We have everything Nheko's alpha had PLUS E2EE recovery chain, self-echo, multi-account (partially), image viewer, reactions, thread support (buggy), message edit/delete. We're missing keyboard nav, cross-signing, and outbound session persistence.

---

## What Makes Progressive Chat Unique

| Feature | Details |
|---|---|
| PineTab/PinePhone optimized | ARM64 build, low RAM (120 MB), Qt-free core for future daemon mode |
| Multi-account | Only Matrix client attempting real multi-account (not profile switching) |
| Binary size | 2.8 MB vs 15-200 MB for competitors |
| Pure C++20 | No JS engine, no Rust SDK, no Electron |
| Qt-free core | Can run headless (20-30 MB) for daemon mode |
| E2EE from scratch | Own implementation, not matrix-rust-sdk or matrix-js-sdk |
| libolm directly | No wrappers — own OlmAccount/OlmSession/MegolmStore |

---

## Target: Alpha Feature Set

Based on this comparison, our alpha should match or exceed Nheko v0.6-level features:
- ✅ E2EE text (done)
- ❌ E2EE files (Phase 4)
- ✅ Threads (fixing bugs)
- ✅ Reactions (done)
- ✅ Room management (done)
- ✅ Image/file upload (done)
- ❌ Keyboard nav (Phase 3)
- ❌ Read receipts receive (Phase 2 bug)
- ❌ Outbound session persistence (Phase 4)
- ❌ Cross-account search/reply (unique feature, Phase 7)

We should NOT attempt to match Nheko v0.12 (VoIP, spaces, cross-signing) for alpha — that's beta scope.

---

*Last updated: July 27, 2026. Research via GitHub READMEs, changelogs, spec v1.19, and explore agent.*
