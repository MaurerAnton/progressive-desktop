# CODE_MAP.md — Where to find every bug, feature, and responsibility

> Generated July 28, updated August 1 — Phase 1 + Phase 2 (SAS verification) complete, live-Synapse CI test, UI polish.
> Use Ctrl+F with feature names (e.g. "thread replies", "E2EE", "room switch").

---

## Quick Reference — If you need to fix...

| Feature / Bug | Primary files | Key lines |
|---|---|---|
| **E2EE multi-account — shared flag** | `decryptor.cpp` + `olm_account.hpp` + `sync_engine.cpp` | `decryptor.cpp:29` (init with shared), `olm_account.hpp:58` (shared_), `sync_engine.cpp:325-395` (uploadDeviceKeys) |
| **E2EE multi-account — per-account scoping** | `session_store.cpp` + `account_switcher.cpp` + `e2ee_init_handler.cpp` | `session_store.cpp:206-230` (saveOlmAccount), `session_store.cpp:243-265` (megolm key format), `account_switcher.cpp:67-85` (switchAccount) |
| **E2EE OTK count tracking** | `sync_engine.cpp` + `olm_account.hpp` | `sync_engine.cpp:343-355` (smart generation), `olm_account.hpp:58` (uploadedKeyCount_) |
| **E2EE device_lists tracking** | `fast_sync.cpp` + `decryptor.cpp` + `sync_engine.cpp` | `fast_sync.cpp:230-245` (parse), `decryptor.cpp` (staleDeviceUsers_), `sync_engine.cpp:225` (mark stale) |
| **E2EE device reset** | `toolbar_handler.cpp` + `matrix_client.cpp` | `toolbar_handler.cpp` (resetDeviceKeys), `matrix_client.cpp` (deleteDevice) |
| **E2EE fallback keys** | `olm_session.cpp` (submodule) + `olm_account.hpp` (desktop) | Real in submodule's `OlmAccountData` API (generateFallbackKey, olm_session.cpp:157). Needs API port to `OlmAccount` class. device_unused_fallback_key_types from /sync parsed (sync_models.cpp). NOT blocked — Phase 3. |
| **E2EE ed25519 verify** | `ed25519.hpp` (new) | `olm_ed25519_verify` — PUBLIC stable libolm API (olm.h:516). Replaces submodule stub. Foundation for all signature verification. |
| **E2EE Olm plaintext validation** | `decryptor.cpp` | `handleOlmEncryptedToDevice` — verify sender/recipient/recipient_keys per m.olm.v1. |
| **E2EE OTK + device key sig verify** | `sig_verify.hpp` + `decryptor.cpp` | Verify signed_curve25519 OTK sigs + device_keys sigs on /keys/query + /keys/claim. Canonical JSON construction mirrors buildKeysUploadBody. |
| **E2EE SAS verification** (Phase 2, complete) | `verification.hpp` + `sas.hpp` + `sas_emojis.hpp` + `verify_controller.cpp` + `decryptor.cpp` | State machine (12 states), OlmSAS crypto (ported from submodule), 64-emoji table, to-device + in-room routing, commitment + MAC verification, DeviceKeyResolverFn via /keys/query. |
| **E2EE SAS UI** | `sas_verification_dialog.cpp` + `verification_handler.cpp` | 7-emoji comparison dialog (match/mismatch/cancel), status-bar accept banner, cancel codes. Entry points: RoomMembersDialog right-click Verify, PrefsDialog 'Your devices'. |
| **E2EE SAS protocol test** | `test_e2ee_verify_protocol.cpp` | Two-manager full request→done flow, emoji match, corrupted-MAC cancel path. |
| **Live-Synapse E2EE test** | `test_synapse_e2ee.cpp` + `.github/workflows/synapse-e2ee.yml` | Real Synapse container: register 2 users → encrypted room → room-key share → cross-account decrypt. Skips when no server (local ctest stays 100%). |
| **E2EE random** | `random.hpp` (new) | `fillCryptoRandom()` — bridge for crypto randomness (swappable provider for WASM). |
| **Color settings** | `color_settings_dialog.cpp` (new) | QScrollArea + QFormLayout with 22 color tokens. Design struct (theme.hpp) — static inline QColor fields, runtime-mutable. Theme::save/load + reapply + addListener. |
| **Shortcuts reference** | `shortcuts_dialog.cpp` (new) | Read-only table of all keyboard shortcuts. Settings → Shortcuts. |
| **Keyboard navigation** | `main_window.cpp` | Alt+Up/Dn rooms, Ctrl+Up/Dn messages, Ctrl+Tab accounts, Ctrl+R reply, Ctrl+K react, Ctrl+L focus list, Esc close thread, Ctrl+1-9 jump room, Ctrl+Shift+C smart copy. |
| **Registration token** | `matrix_client.cpp` + `login_dialog.cpp` | m.login.registration_token flow. Field name: `"token"` NOT `"registration_token"`. Server presets dropdown + save last server. |
| **Portability hardening** | `crash_handler.hpp` + `memory_stats.cpp` + `sync_engine.cpp` | crash_handler #ifdef __linux__ gate, memory_stats/proc/mallinfo #ifdef gates, sync_engine backup path → platform callback. CI: scripts/check_no_qt_in_core.sh. |
| **Prefs system** | `prefs_dialog.cpp` | QSettings: sync poll timeout, history load limit, invisible mode, image cache, last server. Pattern: PrefsDialog static getter + setter pushed from UI layer (Qt-free core maintained). |
| **167 stale OTKs** | Trial-and-error from broken E2EE period | Fixed via device reset + OTK count tracking. Story in `docs/E2EE.md`. |
| **Thread replies — send unencrypted** (security) | `chat_view.cpp` + `thread_handler.cpp` + `matrix_client.cpp` | `chat_view.cpp:121,164` (missing m.relates_to), `thread_handler.cpp:181` (no encryption path) |
| **Thread reply 💬 badge disappears** after room switch | `room_data_loader.cpp` + `room_store.cpp` + `room_handler.cpp:onLoadMoreClicked` | `room_data_loader.cpp:122-161` (thread info parsed but count never incremented), `room_store.cpp:489-498` (during-loop count), `room_handler.cpp:226-244` (same gap) |
| **Thread root body in thread view** (E2EE DEBT B41) | `thread_handler.cpp` | `thread_handler.cpp:92-98` (encrypted root has empty body — no Decryptor*) |
| **Hide from list** not persistent | `room_context_menu.cpp` + `room_list_model.cpp` + `session_store.cpp` | `room_context_menu.cpp:171-179`, `room_list_model.cpp:60` |
| **Image preview** not rendering | `image_loader.cpp` + `timeline_painter.cpp` + `attachment_handler.cpp` | `image_loader.cpp:15-37` (fetchThumbnail), `timeline_painter.cpp:333-373` |
| **Room creation** (only DM, no groups) | `toolbar_handler.cpp` | `toolbar_handler.cpp:140-195` (createNewChatAction — DM only) |
| **Multi-account** | `account_switcher.cpp` + `main_window.cpp` + `login_dialog.cpp` | `account_switcher.cpp`, `main_window.cpp:121-126` |
| **Copy messages** | `timeline_handlers.cpp` | `timeline_handlers.cpp` (handleCopyLink only) |
| **Event source viewer** | Not implemented | — |
| **E2EE inbound decryption** (Bug A) | `decryptor.cpp` + `sync_engine.cpp` | `decryptor.cpp` (decryptMegolmEvent, handleRoomKey, requestRoomKey, forceNewOlmSession) |
| **E2EE outbound encryption** (Bug B) | `chat_view.cpp` + `decryptor.cpp` | `chat_view.cpp:144-219`, `decryptor.cpp` (encryptMessage, shareRoomKey) |
| **E2EE init** (Olm account + megolm load) | `e2ee_init_handler.cpp` + `sync_engine.cpp` | `e2ee_init_handler.cpp:14-89`, `sync_engine.cpp:294-395` (uploadDeviceKeys) |
| **Room rendering** | `timeline_painter.cpp` | Full file — bubble, avatar, image, thread badge, reactions, time |
| **Room list** (sync → upsert) | `room_store.cpp` + `room_list_model.cpp` + `sync_response_handler.cpp` | `room_store.cpp:139-222`, `room_list_model.cpp:60-118` |
| **Room switch** | `room_handler.cpp` | `room_handler.cpp:68-151` |
| **Reactions** (send, add, remove) | `timeline_handlers.cpp` + `timeline_model.cpp` + `chat_view.cpp` | `timeline_handlers.cpp:20-47`, `timeline_model.cpp:237-275` |
| **Invitations** (accept/reject/forget) | `room_handler.cpp` + `room_context_menu.cpp` | `room_handler.cpp:292-355`, `room_context_menu.cpp:38-179` |
| **Login / Auth flow** | `auth_handler.cpp` + `login_dialog.cpp` + `session_bootstrap.cpp` | `auth_handler.cpp`, `login_dialog.cpp`, `session_bootstrap.cpp` |
| **Sync token refresh** (M_UNKNOWN_TOKEN) | `sync_engine.cpp` | `sync_engine.cpp:110-200` |
| **Megolm session persistence** | `megolm_store.cpp` + `session_store.cpp` | `megolm_store.cpp:79-151` (pickleAll/unpickleAll), `session_store.cpp:243-265` |
| **Olm session persistence** | `decryptor.cpp` | `decryptor.cpp:1160-1200` (pickleOlmSessions/unpickleOlmSessions) |
| **Keyboard shortcuts** | `main_window.cpp` | keyPressEvent — Alt+Up/Dn, Ctrl+Up/Dn, Ctrl+Tab, Ctrl+R, Ctrl+K, Ctrl+L, Ctrl+1-9, Ctrl+Shift+C (smart copy), Esc, F11, F12 |
| **Typing indicators** | `room_store.cpp` + `room_list_model.cpp` | `room_store.cpp:171`, `room_list_model.cpp:44-50` |
| **Notifications** | `sync_response_handler.cpp` + `notifications.cpp` | `sync_response_handler.cpp:78-88` |
| **Edits** | `timeline_handlers.cpp` + `messsage_edit.cpp` | `timeline_handlers.cpp:85-110`, `timeline_model.cpp:148-174` |
| **Redactions** | `timeline_handlers.cpp` + `room_store.cpp` | `timeline_handlers.cpp:49-83`, `room_store.cpp:455-458` |
| **Pins** | `timeline_handlers.cpp` + `timeline_model.cpp` | `timeline_handlers.cpp:10-15`, `timeline_model.cpp:277-282` |
| **File upload** | `chat_view.cpp` + `attachment_handler.cpp` | `chat_view.cpp:250-285`, `attachment_handler.cpp` |
| **Room directory** | `room_directory_dialog.cpp` | Full file |
| **Room settings** | `room_settings_dialog.cpp` | Full file |
| **Slash commands** | `slash_command_handler.cpp` | Full file |
| **Emoji picker** | `emoji_picker.cpp` | Full file |
| **Markdown rendering** | `progressive/markdown.hpp` | cmark-gfm wrapper |
| **Dark theme** | `theme.cpp` + `theme.hpp` | Design struct — all colors, font sizes |
| **Memory diagnostics** | `memory_stats.cpp` + `main.cpp:143-188` | `memory_stats.cpp` |

---

## Full File Index — Every source file with responsibility

### src/core/ — Qt-free Matrix logic

| File | Lines | Responsibility |
|---|---|---|
| `matrix_client.cpp/.hpp` | 1263 / 376 | ALL HTTP API calls: send, sync, getMessages, login, keys/upload, etc. 40+ methods. `ApiResult<T>` return type. |
| `http_client.cpp/.hpp` | 223 / 95 | Raw libcurl HTTP layer: `httpGet`, `httpPost`, `httpPut`, SSL, auth headers. |
| `sync_engine.cpp/.hpp` | 368 / 110 | Background sync loop: incremental poll, backoff, M_UNKNOWN_TOKEN recovery, token refresh, to-device processing, device key upload. Owns `Decryptor decryptor_` by value. |
| `fast_sync.cpp/.hpp` | 253 / 115 | `syncFast()` — parses /sync JSON via simdjson into `FastSyncResponse` structs. The performance-critical hot path. |
| `session_store.cpp/.hpp` | 351 / 75 | SQLite persistence: account, sync_token, olm_account, e2ee_data, megolm_sessions, olm_sessions. Uses WAL mode + explicit checkpoint. |
| `crypto/decryptor.cpp/.hpp` | 1190 / 206 | E2EE coordinator: Olm + Megolm. `decryptMegolmEvent`, `handleRoomKey`, `encryptMessage`, `shareRoomKey`, `requestRoomKey`, `forceNewOlmSession`. 8 libolm quirks documented (see docs/E2EE.md). `ctxToken_` workaround (see AGENTS.md #14). **Phase 1 additions**: Olm plaintext validation (sender/recipient/recipient_keys). OTK + device key sig verification (via sig_verify.hpp helpers). **Phase 2 additions**: m.key.verification.* to-device routing (via verification.hpp). |
| `crypto/megolm_store.cpp/.hpp` | 163 / 78 | Inbound Megolm session management: add room_key, decrypt ciphertext, pickle/unpickle. Wraps libolm C API. |
| `crypto/olm_account.cpp/.hpp` | 136 / 68 | Olm account lifecycle: init, identity keys, one-time keys, sign JSON, pickle/unpickle. |
| `crypto/ed25519.hpp/.cpp` | NEW | **Phase 1.** `ed25519Verify()`, `ed25519VerifyJson()` — wraps `olm_ed25519_verify` (PUBLIC stable libolm API). Thread-safe (per-call OlmUtility). |
| `crypto/sig_verify.hpp/.cpp` | NEW | **Phase 1.** `buildDeviceKeysCanonical()`, `verifyDeviceKeys()`, `verifyOtk()` — canonical JSON construction + signature verification. Called from shareRoomKey. |
| `crypto/random.hpp/.cpp` | NEW | **Portability.** `fillCryptoRandom(buf, len)` + `setCryptoRandomProvider(fn)` — swappable crypto randomness bridge (default: std::random_device; WASM: plug in WebCrypto). |
| `crypto/sas.hpp/.cpp` | NEW | **Phase 2.** OlmSAS wrapper: `sasCreate()`, `sasSetTheirKey()`, `sasGenerateBytes()`, `sasCalculateMac()`, `sasVerifyMac()`. Ported from submodule's sas_verification.cpp (REAL, 212L). |
| `crypto/sas_emojis.hpp/.cpp` | NEW | **Phase 2.** 64-emoji table (spec MSC3086), `computeSasEmojis()`, `computeSasDecimals()` (overlapping 13-bit windows), `formatSasEmojis()`. Ported from submodule. |
| `crypto/verification.hpp/.cpp` | NEW | **Phase 2.** SAS state machine: `VerificationTransaction` (8 states), `VerificationManager` (create, handle events, build messages, MAC info, commitment hash, emoji computation). To-device + in-room callback routing. |
| `json_utils.cpp/.hpp` | 51 / 10 | `jsonEscape()`, `jsonUnescape()`, `parseJsonStringValue()`, `parseMatrixErrorJson()`. |
| `debug_log.hpp` | 60 | LOG(Channel, fmt, ...) macro. Channels: GUI, SYNC, E2EE, NET, MEM, DBG. |
| `thread_pool.cpp/.hpp` | 48 / 33 | Global `ThreadPool` singleton — enqueue background work. |
| `account_info.hpp` | 23 | `AccountInfo` struct: userId, accessToken, refreshToken, homeserverUrl, deviceId. |
| `memory_stats.cpp/.hpp` | 85 / 31 | `logMemorySnapshot()`, `trimMemory()`, `logStructSizes()`. |
| `crash_handler.hpp` | 47 | SIGSEGV/SIGABRT handler with stack trace + log dump. |
| `utils.hpp` | 18 | `urlEncodePath()`, `genTxnId()`, `formatTimestamp()`. |

### src/ui/handlers/ — Business logic (orchestration)

| File | Lines | Responsibility |
|---|---|---|
| `room_handler.cpp/.hpp` | 357 / 93 | **Room orchestration**: onRoomClicked (clear timeline, load history, members), load-more, accept/reject invite, open/close thread view, context menu dispatch. Owns ThreadHandler + RoomContextMenu children. **load-more path has NO m.room.encrypted handling** (DEBT). |
| `thread_handler.cpp/.hpp` | 221 / 45 | **Thread view**: openThreadView (fetch /relations, snapshot root, parse replies), closeThreadView, sendThreadReply, replyInThread dialog. **sendThreadReply has NO encryption path** (security bug). **No Decryptor* → can't decrypt E2EE root** (DEBT B41). |
| `room_context_menu.cpp/.hpp` | 302 / 48 | **Context menus**: room list (leave/accept/reject/forget/hide), timeline (reaction/pin/reply/copy/edit/delete). **hideAction has no persistence**. forgetAction chains leave→forget. |
| `sync_response_handler.cpp/.hpp` | 104 / 59 | **Sync → UI bridge**: receives FastSyncResponse from SyncEngine, calls prepareRoomSyncUpdate on thread pool, applyRoomSyncUpdate on UI thread, handles notifications. |
| `auth_handler.cpp/.hpp` | 57 / 44 | Login/logout flow. Shows LoginDialog, emits loggedIn/loggedOut. |
| `toolbar_handler.cpp/.hpp` | 255 / 79 | Toolbar actions: New Chat (DM only — no groups), Join Room, Browse, All Threads, Room Settings, Room Members, Settings, Fullscreen. |
| `session_bootstrap.cpp/.hpp` | 102 / 25 | Post-login bootstrap: start sync, init E2EE, persist crypto, kick off first sync callback. Called from `main_window.cpp:startWithSavedSession()`. |
| `e2ee_init_handler.cpp/.hpp` | 109 / 22 | E2EE initialization: load/save Olm account pickle, load Megolm + Olm sessions, schedule device key upload. Pure init — no ongoing E2EE ops. |
| `account_switcher.cpp/.hpp` | 90 / 55 | Multi-account dropdown + switch logic. Refreshes room list on account switch. |
| `attachment_handler.cpp/.hpp` | 88 / 30 | File attachment: open file dialog → download media → ImageViewerDialog. |
| `slash_command_handler.cpp/.hpp` | 27 / 24 | Slash command parsing: /invite, /leave, /kick, /ban, /join. |

### src/ui/timeline/ — Message display

| File | Lines | Responsibility |
|---|---|---|
| `timeline_model.cpp/.hpp` | 299 / 131 | QAbstractListModel: `DisplayedEvent` vector, seenIds_ dedup, rowIndex_ O(1) lookup. appendBack, appendFront, appendBackBatch, replaceEcho, clear, setImage, addReaction, removeReaction, markDeleted, updateBody, setPinned. Max 200 events (evicts oldest). |
| `timeline_painter.cpp/.hpp` | 427 / 26 | QStyledItemDelegate paint(): message bubbles, avatars, sender names, images, thread 💬 badge, timestamps, reactions, file cards. The most complex drawing code. |
| `timeline_delegate.cpp/.hpp` | 174 / 43 | Delegate: sizeHint, createEditor, setEditorData. Message click + reaction click signals. |
| `timeline_layout.cpp/.hpp` | 135 / 103 | `BubbleLayout` struct: computes heights for name row, body, thread count, reactions. Used by painter's sizeHint. |
| `timeline_handlers.cpp/.hpp` | 112 / 34 | Context menu actions that touch the server: reaction send/delete, pin/unpin, edit, copy link, delete. |

### src/ui/room/ — Room data + sync processing

| File | Lines | Responsibility |
|---|---|---|
| `room_store.cpp/.hpp` | 505 / 109 | **Room sync processing**: `prepareRoomSyncUpdate` (worker thread — builds RoomSyncUpdate from FastSyncResponse), `applyRoomSyncUpdate` (UI thread — upserts rooms, appends timeline). Also hosts shared utils: `extractStringDec`, `msgType`, `msgBody`, `extractThreadRootId`, `extractReplyToId`, `makeSystemBody`. `appendTimelineForRoom` — sync timeline path with E2EE decryption. |
| `room_data_loader.cpp/.hpp` | 296 / 44 | **Async room data loading**: `loadHistory` (GET /messages), `loadMembers` (GET /members), `batchLoadRoomStates` (GET /state for name/avatar/encryption). History path has E2EE decryption but **thread reply count NOT incremented** (bug). |
| `room_list_model.cpp/.hpp` | 176 / 100 | QAbstractListModel for room sidebar. RoomData vector, upsertRoom (sorted by lastActivity), removeRoom, findRowByRoomId, refreshHeader. **No isHidden check** (bug). |
| `event_body_parser.hpp` | 32 | `parsePlaintextBody()` — inline helper that parses decrypted Megolm plaintext JSON → type + contentJson. Used by both sync and history paths. |

### src/ui/chat/ — Message input + sending

| File | Lines | Responsibility |
|---|---|---|
| `chat_view.cpp/.hpp` | 320 / 49 | **Message sending**: doSend (text — both encrypted and unencrypted paths), doAttachFile (uploads), doQuickReact (reaction). **Thread replies in encrypted rooms lose m.relates_to** (bug). **Emotes + file uploads bypass encryption** (DEBT). |
| `messsage_edit.cpp/.hpp` | 178 / 62 | QTextEdit wrapper: send on Enter, emoji button, typing indicator, @mention autocomplete. |
| `emoji_picker.cpp/.hpp` | 398 / 42 | Emoji picker dialog with search and categories. |
| `chat_logger.cpp/.hpp` | 41 / 21 | Chat log: write incoming messages to file (debug feature). |

### src/ui/dialogs/ — Modal dialogs

| File | Lines | Responsibility |
|---|---|---|
| `login_dialog.cpp/.hpp` | 245 / 45 | Login screen: homeserver URL, user/password, discover, login POST, session save. |
| `room_settings_dialog.cpp/.hpp` | 282 / 43 | Room settings: name, topic, avatar, encryption toggle, leave/forget. |
| `room_directory_dialog.cpp/.hpp` | 212 / 45 | Browse public rooms: search, pagination, join. |
| `threads_dialog.cpp/.hpp` | 133 / 33 | All threads list: shows all rooms with active threads. |
| `image_viewer_dialog.cpp/.hpp` | 97 / 38 | Full-size image viewer: zoom, pan, save as. |
| `network_log_dialog.cpp/.hpp` | 139 / 25 | Network debug log viewer. |
| `prefs_dialog.cpp/.hpp` | 53 / 26 | Preferences: sync poll timeout, history load limit, invisible mode, image cache. Static getter pattern — setters pushed from UI layer to keep core Qt-free. |
| `color_settings_dialog.cpp/.hpp` | NEW | Color picker dialog: 22 color tokens in QScrollArea + QFormLayout. Each row has a color swatch button → QColorDialog. On save → Theme::save() → Theme::reapply(). |
| `shortcuts_dialog.cpp/.hpp` | NEW | Read-only reference table of all keyboard shortcuts. Settings → Shortcuts. |
| `sas_dialog.cpp/.hpp` | NEW | **Phase 2.** SAS verification dialog: 7 emoji display, "They match"/"They don't match", cancel/timeout. signals: matchConfirmed, cancelled.

### src/ui/profile/ — User/room member dialogs

| File | Lines | Responsibility |
|---|---|---|
| `room_members_dialog.cpp/.hpp` | 149 / 47 | Room member list: view members, search by name. |
| `user_profile_dialog.cpp/.hpp` | 293 / 45 | User profile: display name, avatar, open DM. |

### src/ui/shared/ — Shared utilities

| File | Lines | Responsibility |
|---|---|---|
| `theme.cpp/.hpp` | 87 / 95 | Dark theme: all QColor tokens (static inline), `applyDarkTheme(app)`, `Design` struct. |
| `image_loader.cpp/.hpp` | 81 / 48 | Async image loading: `fetchThumbnail` (downloadMedia → loadFromData → callback). |
| `notifications.cpp/.hpp` | 55 / 35 | Desktop notifications via D-Bus (Linux) / native API. |

### src/ui/ — Top-level UI

| File | Lines | Responsibility |
|---|---|---|
| `main_window.cpp/.hpp` | 289 / 118 | **Main orchestration**: creates all handlers, widgets, models, delegates. setClient → propagates to 9 handlers. setSessionStore → propagates to roomStore_. wireSyncCallbacks → connects sync → UI bridge. |
| `room_list_delegate.cpp/.hpp` | 263 / 37 | Custom delegate for room sidebar (avatars, unread badges, typing dots, invite accept/reject). |
| `ui_layout_builder.cpp/.hpp` | 170 / 47 | Builds the main window layout from widgets. Returns `UILayout` struct with all pointers. |
| `main.cpp` | 308 | Entry point: CLI test modes + GUI mode with font discovery, SessionStore open, client init, login flow. |

### tests/

| File | Lines | Purpose |
|---|---|---|
| `test_visual.cpp` | 390 | Visual smoke tests: widgets, models, theme. |
| `test_phase1.cpp` | 123 | Phase 1: HTTP client, login, sync basic. |
| `test_phase4.cpp` | 355 | Phase 4: E2EE decryption/encryption tests. |
| `test_olm_inbound.cpp` | 266 | Olm inbound session creation + decryption. |
| `test_megolm_inbound.cpp` | 218 | Megolm inbound session creation + decryption. |
| `test_gui_phase4.cpp` | 284 | GUI + E2EE integration tests. |
| `bench_sync_parse.cpp` | 174 | Performance benchmark for sync JSON parsing. |
| `test_e2ee_account.cpp` | NEW | **Phase 1.** OlmAccountStore shared-flag + uploadedKeyCount lifecycle + save/load roundtrip. |
| `test_e2ee_otk_count.cpp` | NEW | **Phase 1.** OTK generate/publish/count cycle + save/load roundtrip. |
| `test_e2ee_store.cpp` | NEW | **Phase 1.** MegolmStore unpickle with garbage data + Decryptor::markDevicesStale cap at 1000. |
| `test_e2ee_sas.cpp` | NEW | **Phase 2.** SAS crypto roundtrip: Alice+Bob create sessions, exchange pubkeys, compute SAS bytes (must match), calculate+verify MACs. |

---

## Event Parsing Duplication Map

**Fixed July 30:** Created `src/ui/room/event_parser.cpp` — `parseEventFields()` shared helper extracts type, event_id, sender, origin_server_ts, state_key, contentJson, and senderName from simdjson events. Used by `thread_handler.cpp`, `room_data_loader.cpp`, `room_handler.cpp`. `room_store.cpp` not affected (consumes FastEvent, not raw simdjson).

Original duplication (now resolved):

The same pattern (build DisplayedEvent from JSON, extract thread info, set body/msgtype) appears in **4 independent implementations**. When fixing parsing bugs, check ALL four:

| Location | Context | Has decryption? | Has thread info? | Has count logic? |
|---|---|---|---|---|
| `room_store.cpp:313-395` `fastEventToDisplayed` | Sync timeline | Yes (E2EE) | Yes (line 384) | **After loop** (lines 489-498, in appendTimelineForRoom — during-loop, misses same-batch) |
| `room_data_loader.cpp:42-161` `loadHistory` | Initial history load | Yes (E2EE) | Yes (line 127) | **MISSING** (the bug!) |
| `room_handler.cpp:208-251` `onLoadMoreClicked` | Load older messages | **NO** (DEBT) | Yes (line 234) | **MISSING** (the bug!) |
| `thread_handler.cpp:114-141` `openThreadView` chunk loop | Thread replies from /relations | No | No (reply always m.room.message from Synapse) | **Incremented inline at line 210** (send path only) |

---

## Encryption Path Duplication Map

Both sites send encrypted messages. When fixing encryption bugs, check both:

| Location | Context | Has m.relates_to? | Shares room key? |
|---|---|---|---|
| `chat_view.cpp:144-219` | Chat message send (doSend) | **MISSING** for threadRoot (line 164) | Yes (lines 178-206) |
| `thread_handler.cpp:178-218` | Thread reply send (sendThreadReply) | **NO ENCRYPTION AT ALL** (line 181) | No (no encryption path) |

---

## Sync Pipeline

```
/GET /sync JSON
  → fast_sync.cpp:syncFast()            [worker thread — simdjson parse]
  → sync_engine.cpp:run()               [worker thread — processToDevice, call syncCb_]
  → sync_response_handler.cpp:handle()  [UI thread via ThreadPool+QueuedConnection]
    → room_store.cpp:prepareRoomSyncUpdate()  [ThreadPool worker]
    → room_store.cpp:applyRoomSyncUpdate()    [UI thread via QueuedConnection]
      → room_list_model.cpp:upsertRoom()      [model insert/update]
      → room_store.cpp:appendTimelineForRoom() [timeline append + decrypt + reactions]
```

---

## Room Switch Pipeline

```
room_handler.cpp:onRoomClicked(index)
  → clearThreadRoot()
  → threadBanner_->hide()
  → timelineModel_->clear()          [clear seenIds_, events_]
  → chatView_->setCurrentRoom(...)
  → roomStore_->loadHistory(roomId)  [async — GET /messages]
    → room_data_loader.cpp:loadHistory()
      → model->appendBackBatch(events)
      → [MISSING: thread reply count increment] ← THE BUG
  → roomStore_->loadMembers(roomId)  [async — GET /members]
  → messageEdit_->setMembers(...)
```

---

## Message Send Pipeline

```
MessageEdit::enterPressed
  → ChatView::doSend(body)
    → [unencrypted thread reply]: client->sendThreadReply(body, threadRoot)  [chat_view.cpp:122]
    → [encrypted send]: build inner JSON, getOrCreateOutboundSession, encryptMessage,
                        shareRoomKey (if not shared), sendEncryptedEvent  [chat_view.cpp:144-219]
    → [encrypted + thread reply]: falls to encrypted path, but inner JSON MISSES m.relates_to ← BUG
```

---

## Class Dependency Graph

```
MainWindow                    [owner of everything]
├── SyncEngine                [owned by value — mw member sync_]
│   ├── MatrixClient (shared) [passed from main.cpp via setClient]
│   ├── SessionStore (shared) [passed from main.cpp via setSessionStore]
│   └── Decryptor (by value)  [owns decryptor_; exposed as raw ptr]
│       ├── OlmAccountStore
│       └── MegolmStore
├── RoomStore                  [owns dataLoader_]
│   └── RoomDataLoader         [async history + member loading]
├── RoomListModel              [QAbstractListModel — room sidebar]
├── TimelineModel              [QAbstractListModel — message list]
├── ChatView                   [message sending — has encrypt path]
├── RoomHandler                [orchestrates room switch, thread, invites]
│   ├── ThreadHandler          [thread view — HAS NO SYNC_ OR ROOMMODEL_]
│   └── RoomContextMenu        [context menus — hide action, leave/forget]
├── AuthHandler                [login/logout]
├── SyncResponseHandler        [sync → UI bridge]
├── ToolbarHandler             [toolbar actions]
├── AccountSwitcher            [multi-account dropdown]
├── AttachmentHandler          [file upload/view]
├── SlashCommandHandler        [/join, /leave, /kick, /ban]
└── DesktopNotifier            [D-Bus notifications]
```

---

## libolm Quirks (see docs/E2EE.md for details)

| # | Function | Quirk |
|---|---|---|
| 1 | `olm_create_inbound_session` | Expects BASE64 (calls `b64_input` internally) |
| 2 | `olm_decrypt` | Expects BASE64 |
| 3 | `olm_encrypt` | Outputs BASE64 |
| 4 | `olm_init_inbound_group_session` / `olm_import_inbound_group_session` | Both expect BASE64 |
| 5 | `olm_group_encrypt` | Outputs BASE64 |
| 6 | `olm_group_decrypt_max_plaintext_length` | **Mutates ciphertext buffer IN-PLACE** — must memcpy-restore |
| 7 | `olm_*_group_session(void* memory)` / `olm_account()` / `olm_session()` | **NOT pure casts** — call `olm_clear_*()` internally, zeros the struct. Call ONCE on malloc'd memory. |
| 8 | `olm_unpickle_session` / `olm_unpickle_account` / `olm_unpickle_pk_decryption` | **Mutates pickle buffer IN-PLACE** — must pass a copy! |
