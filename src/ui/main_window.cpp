// src/ui/main_window.cpp — Phase 3 full UI.
#include "main_window.hpp"
#include "dialogs/login_dialog.hpp"
#include "dialogs/image_viewer_dialog.hpp"
#include "dialogs/room_directory_dialog.hpp"
#include "dialogs/threads_dialog.hpp"
#include "chat/emoji_picker.hpp"
#include "dialogs/room_settings_dialog.hpp"
#include "profile/room_members_dialog.hpp"
#include "profile_dialog.hpp"
#include "dialogs/prefs_dialog.hpp"
#include "dialogs/network_log_dialog.hpp"
#include "timeline/timeline_handlers.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QAction>
#include <QUrl>
#include <QVBoxLayout>
#include <QClipboard>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QTextCursor>
#include <QUuid>

#include <progressive/markdown.hpp>
#include <progressive/json_parser.hpp>
#include <progressive/event_models.hpp>
#include <progressive/permalink.hpp>

#include <simdjson.h>
#include "core/version.h"
#include "core/memory_stats.hpp"

#include <iostream>
#include <chrono>
#include <thread>
#include <cstdio>

namespace progressive::desktop {

namespace {

// ---- JSON unescape: convert \n, \t, \r, \", \\, \/, \uXXXX to real chars.
// simdjson::to_string() escapes non-ASCII as \uXXXX, so Cyrillic text
// comes through as literal "\u043f\u0440..." — must decode to UTF-8.
std::string jsonUnescape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\') { out += s[i]; continue; }
        if (++i >= s.size()) break;
        char c = s[i];
        switch (c) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'u': {
                // \uXXXX — decode 4 hex digits to UTF-8
                if (i + 4 >= s.size()) { out += 'u'; break; }
                unsigned cp = 0;
                for (int j = 1; j <= 4; ++j) {
                    char hex = s[i + j];
                    cp <<= 4;
                    if (hex >= '0' && hex <= '9') cp |= (hex - '0');
                    else if (hex >= 'a' && hex <= 'f') cp |= (hex - 'a' + 10);
                    else if (hex >= 'A' && hex <= 'F') cp |= (hex - 'A' + 10);
                    else { cp = 0; break; }
                }
                i += 4;
                // Encode codepoint as UTF-8
                if (cp < 0x80) out += static_cast<char>(cp);
                else if (cp < 0x800) {
                    out += static_cast<char>(0xC0 | (cp >> 6));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                } else {
                    out += static_cast<char>(0xE0 | (cp >> 12));
                    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                }
                break;
            }
            default: out += '\\'; out += c; break;
        }
    }
    return out;
}

// ---- JSON string value extraction with escape handling ----
// Finds "key":"value" or "key": "value", returns DECODED string.
std::string extractJsonStringDecoded(std::string_view json, std::string_view key) {
    // Pattern 1: "key":"value"
    std::string pat1 = std::string("\"") + std::string(key) + "\":\"";
    auto pos = json.find(pat1);
    if (pos != std::string_view::npos) {
        pos += pat1.size();
        // Find closing quote, skipping escaped chars
        auto end = pos;
        while (end < json.size()) {
            if (json[end] == '\\' && end + 1 < json.size()) { end += 2; continue; }
            if (json[end] == '"') break;
            ++end;
        }
        if (end < json.size()) return jsonUnescape(json.substr(pos, end - pos));
    }
    // Pattern 2: "key": "value"
    std::string pat2 = std::string("\"") + std::string(key) + "\": \"";
    pos = json.find(pat2);
    if (pos != std::string_view::npos) {
        pos += pat2.size();
        auto end = pos;
        while (end < json.size()) {
            if (json[end] == '\\' && end + 1 < json.size()) { end += 2; continue; }
            if (json[end] == '"') break;
            ++end;
        }
        if (end < json.size()) return jsonUnescape(json.substr(pos, end - pos));
    }
    return {};
}

std::string_view extractLastMessageBody(const std::vector<FastEvent>& events) {
    for (auto it = events.rbegin(); it != events.rend(); ++it) {
        const auto& e = *it;
        if (e.type == "m.room.message" && !e.contentJson.empty()) {
            auto pos = e.contentJson.find("\"body\":\"");
            if (pos != std::string_view::npos) {
                pos += 8;  // skip "body":" (8 chars)
                auto end = pos;
                while (end < e.contentJson.size()) {
                    if (e.contentJson[end] == '\\' && end + 1 < e.contentJson.size()) { end += 2; continue; }
                    if (e.contentJson[end] == '"') break;
                    ++end;
                }
                if (end < e.contentJson.size()) {
                    return e.contentJson.substr(pos, end - pos);
                }
            }
        }
    }
    return {};
}

// Helper: extract a JSON string value from contentJson, handling both
// "key":"value" and "key": "value" (with space) patterns.
// DECODES JSON escapes (\n, \", \uXXXX) — use extractJsonStringDecoded.
std::string extractJsonString(std::string_view json, std::string_view key) {
    return extractJsonStringDecoded(json, key);
}

// Extract thread root event_id from a m.relates_to JSON sub-object.
// Finds the opening { of m.relates_to, then searches for "event_id" within
// that object. This works regardless of field order in the JSON.
// Returns empty string if not a thread reply or event_id not found.
std::string extractThreadRootId(std::string_view contentJson) {
    // Find m.relates_to object
    auto relPos = contentJson.find("\"m.relates_to\"");
    if (relPos == std::string_view::npos) return {};
    // Find the opening { after the key
    auto objStart = contentJson.find('{', relPos);
    if (objStart == std::string_view::npos) return {};
    // Find the matching closing } by depth counting
    int depth = 0;
    size_t objEnd = objStart;
    for (; objEnd < contentJson.size(); ++objEnd) {
        if (contentJson[objEnd] == '{') depth++;
        else if (contentJson[objEnd] == '}') { depth--; if (depth == 0) { objEnd++; break; } }
    }
    // Now extract just the sub-object
    std::string_view relObj = contentJson.substr(objStart, objEnd - objStart);

    // Check if this is a thread relation
    auto rtPos = relObj.find("\"rel_type\":\"m.thread\"");
    if (rtPos == std::string_view::npos) {
        // Try with space: "rel_type": "m.thread"
        rtPos = relObj.find("\"rel_type\": \"m.thread\"");
    }
    if (rtPos == std::string_view::npos) return {};

    // Extract event_id within the sub-object
    auto eidPos = relObj.find("\"event_id\":\"");
    if (eidPos == std::string_view::npos) {
        eidPos = relObj.find("\"event_id\": \"");
        if (eidPos == std::string_view::npos) return {};
        eidPos += 13;  // skip "event_id": "
    } else {
        eidPos += 12;  // skip "event_id":"
    }
    auto eidEnd = relObj.find('"', eidPos);
    if (eidEnd == std::string_view::npos) return {};
    return std::string(relObj.substr(eidPos, eidEnd - eidPos));
}

// Compute room name from state events AND timeline events.
// Returns EMPTY STRING if no name found — caller should keep old name.
// Extract mxc URL from image content JSON
std::string extractMxcUrl(std::string_view contentJson) {
    return extractJsonStringDecoded(contentJson, "url");
}

// Extract mimetype from content JSON
std::string extractMimetype(std::string_view contentJson) {
    return extractJsonStringDecoded(contentJson, "mimetype");
}

// Extract body from content JSON (DECODED)
std::string extractBody(std::string_view contentJson) {
    return extractJsonStringDecoded(contentJson, "body");
}

// Extract msgtype from content JSON
std::string extractMsgtype(std::string_view contentJson) {
    return extractJsonStringDecoded(contentJson, "msgtype");
}

// Extract reply-to event_id from m.in_reply_to relation
std::string extractReplyToId(std::string_view contentJson) {
    auto relPos = contentJson.find("\"m.in_reply_to\"");
    if (relPos == std::string_view::npos) return {};
    auto objStart = contentJson.find('{', relPos);
    if (objStart == std::string_view::npos) return {};
    int depth = 0;
    size_t objEnd = objStart;
    for (; objEnd < contentJson.size(); ++objEnd) {
        if (contentJson[objEnd] == '{') depth++;
        else if (contentJson[objEnd] == '}') { depth--; if (depth == 0) { objEnd++; break; } }
    }
    std::string_view obj = contentJson.substr(objStart, objEnd - objStart);
    auto eidPos = obj.find("\"event_id\":\"");
    if (eidPos == std::string_view::npos) {
        eidPos = obj.find("\"event_id\": \"");
        if (eidPos == std::string_view::npos) return {};
        eidPos += 13;
    } else {
        eidPos += 12;
    }
    auto eidEnd = obj.find('"', eidPos);
    if (eidEnd == std::string_view::npos) return {};
    return std::string(obj.substr(eidPos, eidEnd - eidPos));
}

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("Progressive Chat — Desktop");
    resize(1100, 720);

    // Image loader (created after client is set, but we need it for delegate)
    // We'll set the client pointer later in startWithSavedSession/onLoginDialogAccepted
    imageLoader_ = new ImageLoader(nullptr, this);

    // --- Toolbar ---
    toolbar_ = addToolBar("Main");
    toolbar_->setMovable(false);
    userLabel_ = new QLabel(" Not logged in ", this);
    toolbar_->addWidget(userLabel_);
    toolbar_->addSeparator();

    newChatAction_ = toolbar_->addAction("+ New chat");
    joinRoomAction_ = toolbar_->addAction("Join by ID");
    browseRoomsAction_ = toolbar_->addAction("Browse rooms");
    allThreadsAction_ = toolbar_->addAction("All threads");
    roomSettingsAction_ = toolbar_->addAction("Room settings");
    roomMembersAction_ = toolbar_->addAction("Room members");
    settingsAction_ = toolbar_->addAction("Settings");
    toolbar_->addSeparator();
    fullscreenAction_ = toolbar_->addAction("Fullscreen");

    auto* spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar_->addWidget(spacer);

    accountCombo_ = new QComboBox(this);
    accountCombo_->setMinimumWidth(140);
    accountCombo_->setStyleSheet("QComboBox{background:#1a1a1a;color:#ccc;border:1px solid #333;padding:2px 4px;} QComboBox::drop-down{border:none;} QComboBox QAbstractItemView{background:#1a1a1a;color:#ccc;}");
    toolbar_->addWidget(accountCombo_);
    connect(accountCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onSwitchAccount);

    logoutAction_ = toolbar_->addAction("Logout");

    connect(logoutAction_, &QAction::triggered, auth_, &AuthHandler::logout);
    connect(newChatAction_, &QAction::triggered, this, &MainWindow::onNewChatClicked);
    connect(joinRoomAction_, &QAction::triggered, this, &MainWindow::onJoinRoomClicked);
    connect(browseRoomsAction_, &QAction::triggered, this, &MainWindow::onBrowseRoomsClicked);
    connect(allThreadsAction_, &QAction::triggered, this, &MainWindow::onAllThreadsClicked);
    connect(roomSettingsAction_, &QAction::triggered, this, &MainWindow::onRoomSettingsClicked);
    connect(roomMembersAction_, &QAction::triggered, this, &MainWindow::onRoomMembersClicked);
    connect(settingsAction_, &QAction::triggered, this, &MainWindow::onSettingsClicked);
    connect(fullscreenAction_, &QAction::triggered, this, &MainWindow::onToggleFullscreen);

    // --- Splitter ---
    splitter_ = new QSplitter(Qt::Horizontal, this);

    auto* leftPanel = new QWidget(splitter_);
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);

    roomListHeader_ = new QLabel(" Chats (0) ", this);
    roomListHeader_->setStyleSheet("font-weight:600; padding:10px 12px; color:#e8e8e8; background:#1e1e1e;");
    leftLayout->addWidget(roomListHeader_);

    // Invitations section — visible only when there are pending invites
    inviteHeader_ = new QLabel(" ⬇ Invitations (0) ", this);
    inviteHeader_->setStyleSheet("font-weight:600; padding:6px 12px; color:#ff9944; background:#2a1e1e;");
    inviteHeader_->hide();
    leftLayout->addWidget(inviteHeader_);

    roomList_ = new QListView(leftPanel);
    roomList_->setModel(roomModel_ = new RoomListModel(roomList_));
    roomList_->setStyleSheet(
        "QListView{background:#1e1e1e;border:none;}"
        "QListView::item:hover{background:#2a2a3e;}"
        "QListView::item:selected{background:#3a3a5e;}");
    roomListDelegate_ = new RoomListDelegate(imageLoader_, roomList_);
    roomList_->setItemDelegate(roomListDelegate_);
    connect(roomListDelegate_, &RoomListDelegate::inviteAccepted, this, [this](const QString& roomId) {
        if (!client_) return;
        statusLabel_->setText("Accepting invite...");
        MatrixClient* client = client_;
        QPointer<MainWindow> guard(this);
        std::thread([guard, client, roomId]() {
            auto r = client->joinRoom(roomId.toStdString());
            QMetaObject::invokeMethod(guard, [guard, r, roomId]() {
                if (guard.isNull()) return;
                if (r.ok) {
                    RoomData* rd = const_cast<RoomData*>(guard->roomModel_->at(
                        guard->roomModel_->findRowByRoomId(roomId.toStdString())));
                    if (rd) { rd->isInvite = false; emit guard->roomModel_->dataChanged(
                        guard->roomModel_->index(guard->roomModel_->findRowByRoomId(roomId.toStdString())),
                        guard->roomModel_->index(guard->roomModel_->findRowByRoomId(roomId.toStdString()))); }
                    guard->statusLabel_->setText("Joined room.");
                } else {
                    guard->statusLabel_->setText("Failed: " + QString::fromStdString(r.error.message));
                }
            }, Qt::QueuedConnection);
        }).detach();
    });
    connect(roomListDelegate_, &RoomListDelegate::inviteRejected, this, [this](const QString& roomId) {
        if (!client_) return;
        statusLabel_->setText("Rejecting invite...");
        MatrixClient* client = client_;
        QPointer<MainWindow> guard(this);
        std::thread([guard, client, roomId]() {
            auto r = client->leaveRoom(roomId.toStdString());
            QMetaObject::invokeMethod(guard, [guard, r, roomId]() {
                if (guard.isNull()) return;
                if (r.ok) { guard->roomModel_->removeRoom(roomId.toStdString()); guard->statusLabel_->setText("Invite rejected."); }
                else guard->statusLabel_->setText("Failed: " + QString::fromStdString(r.error.message));
            }, Qt::QueuedConnection);
        }).detach();
    });
    roomList_->setMinimumWidth(280);
    roomList_->setMaximumWidth(400);
    roomList_->setAlternatingRowColors(false);
    roomList_->setUniformItemSizes(true);
    roomList_->setWordWrap(false);
    leftLayout->addWidget(roomList_);

    // Right: timeline + input
    auto* rightPanel = new QWidget(splitter_);
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);

    // Timeline: QListView + delegate (replaces QTextBrowser)
    timelineModel_ = new TimelineModel(this);
    timelineDelegate_ = new TimelineDelegate(imageLoader_, this);
    timelineView_ = new QListView(rightPanel);
    timelineView_->setModel(timelineModel_);
    timelineView_->setItemDelegate(timelineDelegate_);
    timelineView_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    timelineView_->setUniformItemSizes(false);
    timelineView_->setContextMenuPolicy(Qt::CustomContextMenu);
    timelineView_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // Thread banner — visible only when viewing a thread
    threadBanner_ = new QLabel("🧵 Thread — <a href='back' style='color:#6699cc'>← Back to chat</a>", rightPanel);
    threadBanner_->setStyleSheet("background:#2a2a3a; padding:8px; color:#cccccc;");
    threadBanner_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    threadBanner_->hide();
    rightLayout->addWidget(threadBanner_);
    connect(threadBanner_, &QLabel::linkActivated, this, [this](const QString& link) {
        if (link == "back") closeThreadView();
    });

    // Toolbar row: load more + save chat + threads
    auto* timelineToolbar = new QWidget(rightPanel);
    auto* timelineToolbarLayout = new QHBoxLayout(timelineToolbar);
    timelineToolbarLayout->setContentsMargins(4, 2, 4, 2);
    timelineToolbarLayout->setSpacing(4);

    loadMoreBtn_ = new QPushButton("↑ Load older", rightPanel);
    loadMoreBtn_->setStyleSheet("QPushButton{background:#2a2a3a;color:#888;border:1px solid #3a3a3a;padding:2px 10px;border-radius:3px;font-size:11px;}QPushButton:hover{color:#ccc;border-color:#555;}");
    loadMoreBtn_->hide();
    chatLogBtn_ = new QPushButton("💾 Save", rightPanel);
    chatLogBtn_->setCheckable(true);
    chatLogBtn_->setStyleSheet("QPushButton{background:#2a2a3a;color:#888;border:1px solid #3a3a3a;padding:2px 10px;border-radius:3px;font-size:11px;}QPushButton:checked{color:#6c6;border-color:#4a6;}QPushButton:hover{color:#ccc;}");
    chatLogBtn_->hide();
    threadBtn_ = new QPushButton("🧵 Threads", rightPanel);
    threadBtn_->setStyleSheet("QPushButton{background:#2a2a3a;color:#888;border:1px solid #3a3a3a;padding:2px 10px;border-radius:3px;font-size:11px;}QPushButton:hover{color:#ccc;border-color:#555;}");
    threadBtn_->hide();

    timelineToolbarLayout->addWidget(loadMoreBtn_);
    timelineToolbarLayout->addWidget(chatLogBtn_);
    timelineToolbarLayout->addStretch();
    timelineToolbarLayout->addWidget(threadBtn_);
    rightLayout->addWidget(timelineToolbar, 0);
    timelineToolbar->hide();

    connect(loadMoreBtn_, &QPushButton::clicked, this, &MainWindow::onLoadMoreClicked);
    connect(chatLogBtn_, &QPushButton::clicked, this, &MainWindow::onToggleChatLog);
    connect(threadBtn_, &QPushButton::clicked, this, &MainWindow::toggleThreadPanel);

    rightLayout->addWidget(timelineView_, 1);  // stretch factor 1 = fills available space

    timelinePlaceholder_ = new QLabel(
        "Select a chat from the list\nor click \"+ New chat\" to start a conversation", this);
    timelinePlaceholder_->setAlignment(Qt::AlignCenter);
    timelinePlaceholder_->setStyleSheet("color:#969696; font-size:14pt; background:#141414;");
    timelinePlaceholder_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    rightLayout->addWidget(timelinePlaceholder_, 1);
    timelinePlaceholder_->show();
    timelineView_->hide();

    messageEdit_ = new MessageEdit(rightPanel);
    messageEdit_->hide();
    messageEdit_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    rightLayout->addWidget(messageEdit_, 0, Qt::AlignBottom);

    splitter_->addWidget(leftPanel);
    splitter_->addWidget(rightPanel);
    splitter_->setStretchFactor(0, 1);
    splitter_->setStretchFactor(1, 4);

    setCentralWidget(splitter_);
    statusLabel_ = new QLabel("Not synced yet.", this);
    statusBar()->addWidget(statusLabel_, 1);

    wireSyncCallbacks();

    connect(roomList_, &QListView::clicked, this, &MainWindow::onRoomClicked);
    connect(roomList_, &QListView::customContextMenuRequested, this, &MainWindow::onRoomListContextMenu);
    roomList_->setContextMenuPolicy(Qt::CustomContextMenu);

    // ChatView handles message sending, file attach, slash commands, quick react
    chatView_ = new ChatView(client_, timelineModel_, messageEdit_, &sync_, this);
    roomStore_ = new RoomStore(client_, store_);
    auth_ = new AuthHandler(client_, store_, &sync_, userLabel_, statusLabel_, this);
    connect(auth_, &AuthHandler::loggedOut, this, [this]() {
        roomModel_->clear(); timelineModel_->clear();
        timelineView_->hide(); timelinePlaceholder_->show();
        messageEdit_->hide(); currentRoomId_.clear();
        setWindowTitle("Progressive Chat — Desktop");
        auth_->showLoginDialog();
    });
    connect(chatView_, &ChatView::slashCommandForward, this, [this](const std::string& cmd, const std::string& args) {
        onSlashCommand(cmd, args);
    });
    connect(messageEdit_, &MessageEdit::emojiPickerRequested, this, [this]() {
        EmojiPicker picker(this);
        connect(&picker, &EmojiPicker::emojiSelected, this, [this](const QString& emoji) {
            QTextCursor c = messageEdit_->textEdit()->textCursor();
            c.insertText(emoji);
            messageEdit_->setFocus();
        });
        picker.exec();
    });

    // Timeline delegate signals
    connect(timelineDelegate_, &TimelineDelegate::imageClicked, this, &MainWindow::onImageClicked);
    connect(timelineDelegate_, &TimelineDelegate::messageClicked, this, &MainWindow::onMessageClicked);

    // Context menu on timeline
    connect(timelineView_, &QListView::customContextMenuRequested, this, &MainWindow::onTimelineContextMenu);
}

MainWindow::~MainWindow() {
    sync_.stop();
}

void MainWindow::wireSyncCallbacks() {
    sync_.onSync([this](FastSyncResponse resp) {
        QMetaObject::invokeMethod(this, [this, resp = std::move(resp)]() mutable { onSync(std::move(resp)); }, Qt::QueuedConnection);
    });
    sync_.onStateChange([this](SyncEngineState st, const SyncEngineStats& stats) {
        QMetaObject::invokeMethod(this, [this, st, stats]() { onSyncState(st, stats); }, Qt::QueuedConnection);
    });
    sync_.onAuthError([this]() {
        QMetaObject::invokeMethod(auth_, "forceReLogin", Qt::QueuedConnection);
    });
}

void MainWindow::startWithSavedSession() {
    if (!client_ || !client_->isLoggedIn()) return;

    // Generate a unique device_id on first run — prevents token conflicts
    // when multiple installations share the same account.
    auto acct = client_->account();
    if (acct.deviceId.empty() || acct.deviceId == "PROGRESSIVE_DESKTOP") {
        acct.deviceId = "pd-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
        client_->setAccount(acct);
        client_->persistSession();
        std::fprintf(stderr, "[session] generated device_id: %s\n", acct.deviceId.c_str());
    }

    // Log session details for debugging logout issues
    std::fprintf(stderr, "[session] loaded: user=%s device=%s homeserver=%s token_prefix=%s refresh=%s\n",
                 acct.userId.c_str(),
                 acct.deviceId.c_str(),
                 acct.homeserverUrl.c_str(),
                 acct.accessToken.substr(0, 8).c_str(),
                 acct.refreshToken.empty() ? "(none)" : (acct.refreshToken.substr(0, 8) + "...").c_str());

    // Set client on image loader
    imageLoader_->setClient(client_);
    timelineDelegate_->setMyUserId(client_->account().userId);
    // Populate account switcher
    if (store_) {
        auto accounts = store_->listAccounts();
        for (const auto& a : accounts) {
            QString label = QString::fromStdString(a.userId);
            accountCombo_->addItem(label);
            if (a.userId == client_->account().userId)
                accountCombo_->setCurrentIndex(accountCombo_->count() - 1);
        }
    }
    int cacheSize = PrefsDialog::imageCacheSize();
    imageLoader_->setCacheSize(cacheSize);
    std::fprintf(stderr, "[mem] image cache size: %d\n", cacheSize);
    userLabel_->setText(" " + QString::fromStdString(client_->account().userId) + " ");
    statusLabel_->setText("Starting sync...");

    // Initialize E2EE: load saved olm account pickle, or create new one.
    std::string pickleKey = client_->account().userId + "/" + client_->account().deviceId;
    try {
        std::string savedPickle;
        std::string savedKey;
        if (store_) {
            auto saved = store_->loadOlmAccount();
            if (saved) {
                savedPickle = saved->first;
                savedKey = saved->second;
            }
        }
        bool ok = false;
        if (!savedPickle.empty()) {
            ok = sync_.decryptor()->init(savedPickle, savedKey);
            if (!ok) {
                std::cerr << "[e2ee] failed to load saved olm account — creating new one\n";
                ok = sync_.decryptor()->init();
            }
        } else {
            ok = sync_.decryptor()->init();
        }
        if (!ok) {
            std::cerr << "[e2ee] failed to create olm account — continuing without E2EE\n";
        } else {
            auto keys = sync_.decryptor()->identityKeys();
            std::cerr << "[e2ee] olm account ready: curve25519=" << keys.curve25519
                      << " ed25519=" << keys.ed25519 << "\n";
            // Save the account pickle for next launch
            std::string newPickle = sync_.decryptor()->saveAccountPickle(pickleKey);
            if (!newPickle.empty() && store_) {
                store_->saveOlmAccount(newPickle, pickleKey);
            }

            // Load saved megolm sessions (survive app restart)
            if (store_) {
                auto megolmData = store_->loadMegolmSessions();
                if (megolmData && !megolmData->empty()) {
                    sync_.decryptor()->megolm()->unpickleAll(pickleKey, *megolmData);
                    std::cerr << "[e2ee] loaded megolm sessions: " << sync_.decryptor()->megolm()->sessionCount() << "\n";
                }
                auto olmSessionsData = store_->loadOlmSessions();
                if (olmSessionsData && !olmSessionsData->empty()) {
                    sync_.decryptor()->unpickleOlmSessions(pickleKey, *olmSessionsData);
                    std::cerr << "[e2ee] loaded olm session pickles\n";
                }
            }

            // Only upload device keys if not already published
            bool published = store_ ? store_->loadE2eeFlag("keys_published").value_or(false) : false;
            if (!published) {
                std::thread([this]() {
                    sync_.uploadDeviceKeys();
                }).detach();
            } else {
                std::cerr << "[e2ee] device keys already published — skipping upload\n";
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[e2ee] init failed: " << e.what() << "\n";
    }

    // Init desktop notifications (best-effort — fails silently if no tray)
    notifier_.init();

    sync_.setClient(client_);
    sync_.setSessionStore(store_);
    logMemorySnapshot("before-first-sync");
    sync_.start();
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (store_ && sync_.decryptor()->isInitialized()) {
        std::string pickleKey = client_->account().userId + "/" + client_->account().deviceId;
        auto megolmPickle = sync_.decryptor()->megolm()->pickleAll(pickleKey);
        if (!megolmPickle.empty()) store_->saveMegolmSessions(megolmPickle);
        auto olmSessionsPickle = sync_.decryptor()->pickleOlmSessions(pickleKey);
        if (!olmSessionsPickle.empty()) store_->saveOlmSessions(olmSessionsPickle);
    }
    sync_.stop();
    QMainWindow::closeEvent(e);
}

void MainWindow::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_F11) { onToggleFullscreen(); e->accept(); return; }
    QMainWindow::keyPressEvent(e);
}

void MainWindow::onToggleFullscreen() {
    if (!isFullscreen_) { showFullScreen(); isFullscreen_ = true; fullscreenAction_->setText("Exit fullscreen"); }
    else { showNormal(); isFullscreen_ = false; fullscreenAction_->setText("Fullscreen"); }
}

void MainWindow::onRoomListContextMenu(const QPoint& pos) {
    auto idx = roomList_->indexAt(pos);
    if (!idx.isValid()) return;
    const RoomData* r = roomModel_->at(idx.row());
    if (!r) return;

    QMenu menu(this);
    auto* leaveAction = menu.addAction("Leave room");
    auto* acceptAction = menu.addAction("Accept invite");
    auto* rejectAction = menu.addAction("Reject invite");
    // Show only the relevant action based on room state
    if (r->isInvite) {
        leaveAction->setVisible(false);
    } else {
        acceptAction->setVisible(false);
        rejectAction->setVisible(false);
    }

    auto* selected = menu.exec(roomList_->mapToGlobal(pos));
    if (!selected) return;

    if (selected == leaveAction) {
        auto reply = QMessageBox::question(this, "Leave room",
            QString("Leave '%1'?").arg(QString::fromStdString(r->name)),
            QMessageBox::Yes | QMessageBox::No);
        if (reply != QMessageBox::Yes) return;

        std::string roomId = r->roomId;
        MatrixClient* client = client_;
        QPointer<MainWindow> guard(this);
        statusLabel_->setText("Leaving room...");
        std::thread([guard, client, roomId]() {
            auto r = client->leaveRoom(roomId);
            QMetaObject::invokeMethod(guard, [guard, r, roomId]() {
                if (guard.isNull()) return;
                if (r.ok) {
                    guard->statusLabel_->setText("Left room.");
                    guard->roomModel_->removeRoom(roomId);
                    if (guard->currentRoomId_.toStdString() == roomId) {
                        guard->currentRoomId_.clear();
                        guard->timelineModel_->clear();
                        guard->timelineView_->hide();
                        guard->timelinePlaceholder_->show();
                        guard->messageEdit_->hide();
                        guard->loadMoreBtn_->parentWidget()->hide();
                        guard->chatLogging_ = false;
                        guard->chatLogFile_.reset();
                        guard->chatLogBtn_->setChecked(false);
                        guard->chatLogBtn_->setText("💾 Save");
                    }
                } else {
                    guard->statusLabel_->setText("Failed to leave: " + QString::fromStdString(r.error.message));
                }
            }, Qt::QueuedConnection);
        }).detach();
    } else if (selected == acceptAction) {
        std::string roomId = r->roomId;
        MatrixClient* client = client_;
        QPointer<MainWindow> guard(this);
        statusLabel_->setText("Joining room...");
        std::thread([guard, client, roomId]() {
            auto r = client->joinRoom(roomId);
            QMetaObject::invokeMethod(guard, [guard, r, roomId]() {
                if (guard.isNull()) return;
                if (r.ok) {
                    guard->statusLabel_->setText("Joined room.");
                    // Mark room as no longer invite — next sync will update properly.
                    RoomData* rd = const_cast<RoomData*>(guard->roomModel_->at(
                        guard->roomModel_->findRowByRoomId(roomId)));
                    if (rd) {
                        rd->isInvite = false;
                        // Trigger a model update via dataChanged
                        int row = guard->roomModel_->findRowByRoomId(roomId);
                        if (row >= 0) {
                            QModelIndex idx = guard->roomModel_->index(row);
                            emit guard->roomModel_->dataChanged(idx, idx);
                        }
                    }
                } else {
                    guard->statusLabel_->setText("Failed to join: " + QString::fromStdString(r.error.message));
                }
            }, Qt::QueuedConnection);
        }).detach();
    } else if (selected == rejectAction) {
        std::string roomId = r->roomId;
        MatrixClient* client = client_;
        QPointer<MainWindow> guard(this);
        statusLabel_->setText("Rejecting invite...");
        std::thread([guard, client, roomId]() {
            auto r = client->leaveRoom(roomId);  // leave == reject invite
            QMetaObject::invokeMethod(guard, [guard, r, roomId]() {
                if (guard.isNull()) return;
                if (r.ok) {
                    guard->statusLabel_->setText("Invite rejected.");
                    guard->roomModel_->removeRoom(roomId);
                } else {
                    guard->statusLabel_->setText("Failed to reject: " + QString::fromStdString(r.error.message));
                }
            }, Qt::QueuedConnection);
        }).detach();
    }
}

void MainWindow::onRoomClicked(const QModelIndex& idx) {
    const RoomData* r = roomModel_->at(idx.row());
    if (!r) return;
    currentRoomId_ = QString::fromStdString(r->roomId);
    // Close thread view if open
    currentThreadRoot_.clear();
    threadBanner_->hide();
    currentPrevBatch_.clear();
    chatLogging_ = false;
    chatLogBtn_->setChecked(false);
    chatLogFile_.reset();
    chatView_->setCurrentRoom(r->roomId, currentThreadRoot_.toStdString(), r->isEncrypted);
    timelineModel_->clear();
    timelinePlaceholder_->hide();
    timelineView_->show();
    messageEdit_->show();
    loadMoreBtn_->hide();
    chatLogBtn_->show();
    threadBtn_->show();
    loadMoreBtn_->parentWidget()->show();  // show the toolbar that contains these buttons
    messageEdit_->setFocus();
    setWindowTitle(QString("Progressive Chat — %1").arg(QString::fromStdString(r->name)));

    // Read marker: mark room as read up to the latest known event
    if (client_ && client_->isLoggedIn()) {
        int lastRow = timelineModel_->rowCount(QModelIndex()) - 1;
        if (lastRow >= 0) {
            auto* evt = timelineModel_->at(lastRow);
            if (evt && !evt->eventId.empty()) {
                std::string roomIdStr = r->roomId;
                std::string eventIdStr = evt->eventId;
                MatrixClient* client = client_;
                std::thread([client, roomIdStr, eventIdStr]() {
                    client->setReadMarker(roomIdStr, eventIdStr);
                }).detach();
            }
        }
    }

    // Pre-load member avatars from room state for better timeline display
    std::vector<std::string> senderIds;
    for (int i = 0; i < timelineModel_->rowCount(QModelIndex()); ++i) {
        auto* evt = timelineModel_->at(i);
        if (evt && !evt->senderId.empty())
            senderIds.push_back(evt->senderId);
    }
    roomStore_->loadMembers(r->roomId, QPointer<QWidget>(this), senderIds,
        [this](std::vector<MemberInfo> members) {
            for (const auto& m : members) {
                if (!m.avatarUrl.empty()) memberAvatarCache_[m.userId] = m.avatarUrl;
                if (!m.displayName.empty()) memberAvatarCache_[m.userId + "/name"] = m.displayName;
            }
        });

    // Set mention autocomplete names
    QStringList memberNames;
    for (const auto& [key, val] : memberAvatarCache_) {
        if (key.find("/name") != std::string::npos) {
            memberNames << QString::fromStdString(val);
        }
    }
    messageEdit_->setMembers(memberNames);

    // Load room history
    roomStore_->loadHistory(r->roomId, timelineModel_,
        QPointer<QWidget>(this), [this](int count, const std::string& prevBatch) {
            if (count > 0) {
                currentPrevBatch_ = prevBatch;
                if (loadMoreBtn_ && !prevBatch.empty()) loadMoreBtn_->show();
                timelineView_->scrollToBottom();
                statusLabel_->setText(QString("Loaded %1 messages.").arg(count));
            }
        });
}


void MainWindow::onSlashCommand(const std::string& cmd, const std::string& args) {
    if (cmd == "help") {
        DisplayedEvent sys;
        sys.type = "m.room.message";
        sys.msgtype = "m.notice";
        sys.body = "Commands: /help /clear /logout /me <action>";
        sys.senderName = "system";
        timelineModel_->appendBack(sys);
    } else if (cmd == "clear") {
        timelineModel_->clear();
    } else if (cmd == "logout") {
        onLogoutClicked();
    }
}

void MainWindow::onLogoutClicked() {
    sync_.stop();
    if (client_) client_->logout();
    if (store_) store_->clearAccount();
    roomModel_->clear();
    timelineModel_->clear();
    timelineView_->hide();
    timelinePlaceholder_->show();
    messageEdit_->hide();
    currentRoomId_.clear();
    userLabel_->setText(" Not logged in ");
    setWindowTitle("Progressive Chat — Desktop");
    statusLabel_->setText("Logged out.");
    showLoginDialog();
}

void MainWindow::showLoginDialog() {
    LoginDialog dlg(client_, store_, this);
    connect(&dlg, &QDialog::accepted, this, &MainWindow::onLoginDialogAccepted);
    dlg.exec();
}

void MainWindow::onLoginDialogAccepted() {
    if (!client_ || !client_->isLoggedIn()) return;
    userLabel_->setText(" " + QString::fromStdString(client_->account().userId) + " ");
    statusLabel_->setText("Starting sync...");
    sync_.setClient(client_);
    sync_.setSessionStore(store_);
    sync_.start();
}

void MainWindow::forceReLogin() {
    sync_.stop();
    // Soft logout: DON'T clear the model, timeline, or account data.
    // Show a warning message, then the login dialog. After re-login,
    // the sync continues with the new token — all rooms/messages preserved.
    QMessageBox::warning(this, "Session Expired",
        "Your session has expired. This can happen if:\n"
        "  - The server was restarted\n"
        "  - You changed your password\n"
        "  - You logged out from another device\n"
        "  - The app was force-closed during a sync\n\n"
        "Your chats and settings are preserved. Please login again.");
    userLabel_->setText(" [Session expired] ");
    statusLabel_->setStyleSheet("color: red; font-weight: bold;");
    statusLabel_->setText("Session expired — login required");
    showLoginDialog();
    // After successful login, onLoginDialogAccepted will restart sync
    // with the new token. Rooms/messages stay in the model.
    statusLabel_->setStyleSheet("");
}

void MainWindow::onNewChatClicked() {
    if (!client_ || !client_->isLoggedIn()) return;
    bool ok;
    QString userId = QInputDialog::getText(this, "New direct chat",
        "Enter Matrix user ID (e.g. @bob:matrix.org):", QLineEdit::Normal, "@", &ok);
    if (!ok || userId.trimmed().isEmpty()) return;
    userId = userId.trimmed();
    if (!userId.startsWith("@")) userId = "@" + userId;

    statusLabel_->setText("Creating direct chat...");
    std::string uid = userId.toStdString();
    MatrixClient* client = client_;
    QPointer<MainWindow> guard(this);
    std::thread([guard, client, uid]() {
        auto r = client->startDirectMessage(uid);
        QMetaObject::invokeMethod(guard, [guard, r]() {
            if (guard.isNull()) return;
            if (r.ok) guard->statusLabel_->setText("Created room: " + QString::fromStdString(r.data));
            else { QMessageBox::warning(guard, "Error", QString("Failed: %1").arg(QString::fromStdString(r.error.message)));
                   guard->statusLabel_->setText("Failed."); }
        }, Qt::QueuedConnection);
    }).detach();
}

void MainWindow::onJoinRoomClicked() {
    if (!client_ || !client_->isLoggedIn()) return;
    bool ok;
    QString roomIdOrAlias = QInputDialog::getText(this, "Join room",
        "Enter room ID or alias (e.g. #matrix:matrix.org):", QLineEdit::Normal, "", &ok);
    if (!ok || roomIdOrAlias.trimmed().isEmpty()) return;
    QString input = roomIdOrAlias.trimmed();
    // Accept matrix.to permalinks: https://matrix.to/#/#room:server
    int hashIdx = input.indexOf("#/#");
    if (hashIdx >= 0) input = input.mid(hashIdx + 2);
    // Also accept matrix.to with room ID: https://matrix.to/#/!id:server
    int idIdx = input.indexOf("#/!");
    if (idIdx >= 0) input = input.mid(idIdx + 2);

    statusLabel_->setText("Joining...");
    std::string id = input.toStdString();
    MatrixClient* client = client_;
    QPointer<MainWindow> guard(this);
    std::thread([guard, client, id]() {
        auto r = client->joinRoom(id);
        QMetaObject::invokeMethod(guard, [guard, r]() {
            if (guard.isNull()) return;
            if (r.ok) { guard->statusLabel_->setText("Joined: " + QString::fromStdString(r.data));
                        QMessageBox::information(guard, "Joined", "Successfully joined room."); }
            else { QMessageBox::warning(guard, "Error", QString("Failed: %1").arg(QString::fromStdString(r.error.message)));
                   guard->statusLabel_->setText("Failed."); }
        }, Qt::QueuedConnection);
    }).detach();
}

void MainWindow::onBrowseRoomsClicked() {
    if (!client_ || !client_->isLoggedIn()) return;
    RoomDirectoryDialog dlg(client_, this);
    dlg.exec();
    if (!dlg.joinedRoomId().isEmpty()) {
        // Pre-populate the room model with the name from the directory listing.
        // This ensures the room shows up with the correct name immediately,
        // even before /sync delivers the full state.
        RoomData rd;
        rd.roomId = dlg.joinedRoomId().toStdString();
        rd.name = dlg.joinedRoomName().toStdString();
        rd.lastMessage = "Joined";
        rd.lastActivityTs = static_cast<int64_t>(QDateTime::currentMSecsSinceEpoch());
        roomModel_->upsertRoom(rd);
        statusLabel_->setText("Joined room: " + dlg.joinedRoomName());
    }
}

void MainWindow::onAllThreadsClicked() {
    if (!client_ || !client_->isLoggedIn()) {
        QMessageBox::information(this, "Threads", "Please login first.");
        return;
    }
    if (currentRoomId_.isEmpty()) {
        QMessageBox::information(this, "Threads",
            "No room selected.\n\nSelect a chat from the list first, then click 'All threads' to see threads in that room.");
        return;
    }
    ThreadsDialog dlg(client_, currentRoomId_.toStdString(), this);
    dlg.exec();
}

void MainWindow::onRoomSettingsClicked() {
    if (currentRoomId_.isEmpty() || !client_) {
        QMessageBox::information(this, "Settings", "Select a room first.");
        return;
    }
    std::string roomName = "Room";
    auto* rd = roomModel_->at(roomModel_->findRowByRoomId(currentRoomId_.toStdString()));
    if (rd) roomName = rd->name;
    RoomSettingsDialog dlg(client_, currentRoomId_.toStdString(), roomName, this);
    dlg.exec();
}

void MainWindow::onRoomMembersClicked() {
    if (currentRoomId_.isEmpty() || !client_) {
        QMessageBox::information(this, "Members", "Select a room first.");
        return;
    }
    RoomMembersDialog dlg(client_, currentRoomId_.toStdString(), this);
    dlg.exec();
}

void MainWindow::onSwitchAccount(int index) {
    if (index < 0 || !client_ || !store_) return;
    auto accounts = store_->listAccounts();
    if (index >= (int)accounts.size()) return;

    auto& acct = accounts[index];
    if (acct.userId == client_->account().userId) return;

    // Block re-entry during switch
    accountCombo_->setEnabled(false);

    sync_.stop();

    // Save E2EE sessions for current account before switching
    std::string oldKey = client_->account().userId + "/" + client_->account().deviceId;
    if (sync_.decryptor() && sync_.decryptor()->isInitialized()) {
        auto mp = sync_.decryptor()->megolm()->pickleAll(oldKey);
        if (!mp.empty()) store_->saveMegolmSessions(mp);
        auto op = sync_.decryptor()->pickleOlmSessions(oldKey);
        if (!op.empty()) store_->saveOlmSessions(op);
    }

    // Clear UI state
    roomModel_->clear();
    timelineModel_->clear();
    timelineView_->hide();
    timelinePlaceholder_->show();
    messageEdit_->hide();
    currentRoomId_.clear();
    memberAvatarCache_.clear();
    chatView_->clear();

    // Switch account
    client_->setAccount(acct);

    // Re-init E2EE for new account
    std::string newKey = acct.userId + "/" + acct.deviceId;
    if (sync_.decryptor() && sync_.decryptor()->init()) {
        if (store_) {
            auto saved = store_->loadOlmAccount();
            if (saved) sync_.decryptor()->init(saved->first, saved->second);
            auto md = store_->loadMegolmSessions();
            if (md) sync_.decryptor()->megolm()->unpickleAll(newKey, *md);
            auto od = store_->loadOlmSessions();
            if (od) sync_.decryptor()->unpickleOlmSessions(newKey, *od);
        }
        sync_.uploadDeviceKeys();
    }

    // Update UI
    userLabel_->setText(" " + QString::fromStdString(acct.userId) + " ");
    timelineDelegate_->setMyUserId(acct.userId);
    imageLoader_->setClient(client_);
    accountCombo_->setCurrentIndex(index);
    accountCombo_->setEnabled(true);

    sync_.setClient(client_);
    sync_.setSessionStore(store_);
    sync_.start();
}

void MainWindow::onSettingsClicked() {
    // Show a small menu: About / My Profile / Preferences
    QMenu menu(this);
    auto* aboutAction = menu.addAction("About");
    auto* profileAction = menu.addAction("My profile...");
    auto* prefsAction = menu.addAction("Preferences...");
    menu.addSeparator();
    auto* netLogAction = menu.addAction("Network log");
    auto* selected = menu.exec(QCursor::pos());
    if (!selected) return;

    if (selected == aboutAction) {
        QMessageBox::information(this, "About",
            QString("Progressive Chat — Desktop\n\nVersion: %1\nPhase: 4 (E2EE)\n\n"
                    "Toolbar buttons:\n"
                    "  + New chat — start a DM\n"
                    "  Join by ID — join room by ID/alias\n"
                    "  Browse rooms — search public rooms\n"
                    "  All threads — view threads in current room\n"
                    "  Room settings — topic, name, members, kick/ban/promote\n"
                    "  Fullscreen — toggle fullscreen (F11)\n"
                    "  Logout — sign out\n\n"
                    "In chat:\n"
                    "  Right-click message — react, pin/unpin, reply in thread, copy link, redact\n"
                    "  Click image — zoom + open externally\n\n"
                    "Slash commands: /help /clear /logout /me <action>")
                .arg(QString::fromUtf8(PROGRESSIVE_DESKTOP_VERSION)));
    } else if (selected == profileAction) {
        if (!client_ || !client_->isLoggedIn()) {
            QMessageBox::warning(this, "Not logged in", "Please login first.");
            return;
        }
        ProfileDialog dlg(client_, this);
        dlg.exec();
    } else if (selected == prefsAction) {
        PrefsDialog dlg(this);
        if (dlg.exec() == QDialog::Accepted && imageLoader_) {
            imageLoader_->setCacheSize(PrefsDialog::imageCacheSize());
            statusLabel_->setText("Preferences saved. Restart for full effect.");
        }
    } else if (selected == netLogAction) {
        NetworkLogDialog dlg(this);
        dlg.exec();
    }
}

void MainWindow::onImageClicked(const QString& eventId, const QString& mxcUrl) {
    if (mxcUrl.isEmpty()) return;

    // Check the msgtype — videos need to be opened externally, not as QImage
    int row = timelineModel_->findRow(eventId.toStdString());
    QString msgtype;
    QString mimetype;
    if (row >= 0) {
        auto* evt = timelineModel_->at(row);
        if (evt) {
            msgtype = QString::fromStdString(evt->msgtype);
            mimetype = QString::fromStdString(evt->mimetype);
        }
    }

    std::string mxc = mxcUrl.toStdString();
    MatrixClient* client = client_;
    QPointer<MainWindow> guard(this);

    // For videos/audio/files: download to temp file and open with system player
    if (msgtype == "m.video" || msgtype == "m.audio" || msgtype == "m.file") {
        statusLabel_->setText("Downloading " + msgtype.mid(2) + "...");
        std::thread([guard, client, mxc, msgtype]() {
            auto r = client->downloadMedia(mxc, 0, 0);
            QMetaObject::invokeMethod(guard, [guard, r, msgtype]() {
                if (guard.isNull()) return;
                if (!r.ok || r.data.empty()) {
                    QMessageBox::warning(guard, "Error", "Failed to download " + msgtype + ".");
                    guard->statusLabel_->setText("Failed.");
                    return;
                }
                // Save to temp file
                QString suffix = msgtype == "m.video" ? ".mp4" : (msgtype == "m.audio" ? ".mp3" : ".bin");
                QString tempPath = QDir::tempPath() + "/progressive_" +
                    QString::number(QDateTime::currentMSecsSinceEpoch()) + suffix;
                QFile f(tempPath);
                if (!f.open(QIODevice::WriteOnly)) {
                    QMessageBox::warning(guard, "Error", "Failed to create temp file.");
                    return;
                }
                f.write(reinterpret_cast<const char*>(r.data.data()), r.data.size());
                f.close();
                // Open with system default app
                QDesktopServices::openUrl(QUrl::fromLocalFile(tempPath));
                guard->statusLabel_->setText("Opened " + msgtype.mid(2) + ".");
            }, Qt::QueuedConnection);
        }).detach();
        return;
    }

    // For images: show in the image viewer dialog
    statusLabel_->setText("Loading full image...");
    std::thread([guard, client, mxc]() {
        auto r = client->downloadMedia(mxc, 0, 0);
        QImage img;
        if (r.ok && !r.data.empty()) {
            img.loadFromData(r.data.data(), static_cast<int>(r.data.size()));
        }
        QMetaObject::invokeMethod(guard, [guard, img, mxc]() {
            if (guard.isNull()) return;
            if (img.isNull()) {
                QMessageBox::warning(guard, "Error", "Failed to load image.");
                guard->statusLabel_->setText("Failed to load image.");
                return;
            }
            auto* dlg = new ImageViewerDialog(img, QString::fromStdString(mxc), guard);
            dlg->exec();
            delete dlg;
            guard->statusLabel_->setText("Ready.");
        }, Qt::QueuedConnection);
    }).detach();
}

void MainWindow::onMessageClicked(const QString& eventId) {
    int row = timelineModel_->findRow(eventId.toStdString());
    if (row >= 0) {
        QModelIndex idx = timelineModel_->index(row);
        timelineView_->selectionModel()->select(idx, QItemSelectionModel::ClearAndSelect);
    }
}

void MainWindow::onTimelineContextMenu(const QPoint& pos) {
    auto idx = timelineView_->indexAt(pos);
    if (!idx.isValid()) return;
    QString eventId = idx.data(TimelineModel::EventIdRole).toString();
    if (eventId.isEmpty()) return;
    auto globalPos = timelineView_->mapToGlobal(pos);
    showTimelineContextMenu(eventId, globalPos);
}

void MainWindow::showTimelineContextMenu(const QString& eventId, const QPoint& globalPos) {
    QMenu menu(this);
    auto* reactAction = menu.addAction("Add reaction...");

    auto* removeReactMenu = menu.addMenu("Remove reaction");
    std::string eidStr = eventId.toStdString();
    int row = timelineModel_->findRow(eidStr);
    std::string myUserId = client_ ? client_->account().userId : "";
    bool hasReactions = false;
    if (row >= 0) {
        auto* evt = timelineModel_->at(row);
        if (evt) {
            for (const auto& r : evt->reactions) {
                for (const auto& uid : r.userIds) {
                    if (uid == myUserId) {
                        auto* action = removeReactMenu->addAction(QString::fromStdString(r.emoji));
                        connect(action, &QAction::triggered, this, [this, eventId, eidStr, r]() {
                            if (currentRoomId_.isEmpty() || !client_) return;
                            std::thread([this, roomId = currentRoomId_.toStdString(), reid = r.reactionEventId, eidStr, emoji = r.emoji]() {
                                auto res = client_->redactEvent(roomId, reid);
                                QMetaObject::invokeMethod(this, [this, res, eidStr, emoji]() {
                                    if (res.ok) {
                                        timelineModel_->removeReaction(eidStr, emoji, client_->account().userId);
                                        statusLabel_->setText("Reaction removed.");
                                    }
                                }, Qt::QueuedConnection);
                            }).detach();
                        });
                        hasReactions = true;
                    }
                }
            }
        }
    }
    if (!hasReactions) removeReactMenu->setEnabled(false);

    menu.addSeparator();
    auto* pinAction = menu.addAction("Pin message");
    auto* unpinAction = menu.addAction("Unpin message");
    auto* replyThreadAction = menu.addAction("Reply in thread");
    auto* viewThreadAction = menu.addAction("View thread replies");
    bool canViewThread = false;
    QString threadRootForView;
    {
        int row = timelineModel_->findRow(eventId.toStdString());
        if (row >= 0) {
            auto* evt = timelineModel_->at(row);
            if (evt) {
                if (evt->threadReplyCount > 0) { canViewThread = true; threadRootForView = eventId; }
                else if (evt->isThreadReply && !evt->threadRootId.empty()) {
                    canViewThread = true; threadRootForView = QString::fromStdString(evt->threadRootId);
                }
            }
        }
    }
    if (!canViewThread) viewThreadAction->setEnabled(false);
    auto* copyLinkAction = menu.addAction("Copy permalink");
    menu.addSeparator();
    auto* editAction = menu.addAction("Edit");
    auto* deleteAction = menu.addAction("Delete");

    auto* selected = menu.exec(globalPos);
    if (!selected) return;

    std::string roomIdStr = currentRoomId_.toStdString();
    std::string eidStrVal = eventId.toStdString();

    if (selected == reactAction) {
        handleReaction(this, client_, roomIdStr, eidStrVal, timelineModel_, statusLabel_);
    } else if (selected == pinAction) {
        handlePin(this, client_, roomIdStr, eidStrVal, timelineModel_, statusLabel_);
    } else if (selected == unpinAction) {
        std::thread([this, roomIdStr, eidStrVal]() {
            auto r = client_->unpinMessage(roomIdStr, eidStrVal);
            QMetaObject::invokeMethod(this, [this, r, eidStrVal]() {
                if (r.ok) { timelineModel_->setPinned(eidStrVal, false); statusLabel_->setText("Message unpinned."); }
            }, Qt::QueuedConnection);
        }).detach();
    } else if (selected == replyThreadAction) {
        if (currentRoomId_.isEmpty() || !client_) return;
        QString rootText;
        int row = timelineModel_->findRow(eventId.toStdString());
        if (row >= 0) { auto* evt = timelineModel_->at(row); if (evt) rootText = QString::fromStdString(evt->body); }
        bool ok;
        QString reply = QInputDialog::getText(this, "Reply in thread",
            QString("Replying to:\n\"%1\"\n\nYour reply:").arg(rootText.left(100)), QLineEdit::Normal, "", &ok);
        if (!ok || reply.trimmed().isEmpty()) return;
        std::thread([this, roomIdStr, eidStrVal, body = reply.toStdString()]() {
            auto r = client_->sendThreadReply(roomIdStr, body, eidStrVal);
            QMetaObject::invokeMethod(this, [this, r, eidStrVal]() {
                if (r.ok) {
                    int rootRow = timelineModel_->findRow(eidStrVal);
                    if (rootRow >= 0) {
                        auto* rootEvt = timelineModel_->at(rootRow);
                        if (rootEvt) { rootEvt->threadReplyCount++; emit timelineModel_->dataChanged(timelineModel_->index(rootRow), timelineModel_->index(rootRow)); }
                    }
                }
            }, Qt::QueuedConnection);
        }).detach();
    } else if (selected == viewThreadAction) {
        openThreadView(threadRootForView.isEmpty() ? eventId : threadRootForView);
    } else if (selected == copyLinkAction) {
        handleCopyLink(this, roomIdStr, eidStrVal, statusLabel_);
    } else if (selected == editAction) {
        handleEdit(this, client_, roomIdStr, eidStrVal, timelineModel_, statusLabel_);
    } else if (selected == deleteAction) {
        handleDelete(this, client_, roomIdStr, eidStrVal, timelineModel_, statusLabel_);
    }
}

void MainWindow::openThreadView(const QString& rootEventId) {
    if (!client_ || currentRoomId_.isEmpty()) return;
    currentThreadRoot_ = rootEventId;
    timelineModel_->clear();
    threadBanner_->show();
    statusLabel_->setText("Loading thread...");

    // #10: Insert the root message at the top of the thread view.
    // Find it in the (about-to-be-cleared) model before clearing.
    // Actually, we already cleared. We need to fetch the root event from the
    // server via /relations/{eventId} (returns the event itself + its relations).
    // Or: we can use the /event/{eventId} endpoint.
    // For simplicity: if we have the root message cached, add it first.
    // The caller (context menu) has access to timelineModel_->at(row) which
    // has the root message. We'll pass it via a member variable.

    std::string roomId = currentRoomId_.toStdString();
    std::string rootEid = rootEventId.toStdString();
    MatrixClient* client = client_;
    QPointer<MainWindow> guard(this);

    std::thread([guard, client, roomId, rootEid]() {
        // Fetch thread replies via /relations/{eventId}/m.thread
        auto r = client->getThreadReplies(roomId, rootEid);
        QMetaObject::invokeMethod(guard, [guard, r, rootEid]() {
            if (guard.isNull()) return;
            if (!r.ok) {
                guard->statusLabel_->setText("Failed to load thread: " + QString::fromStdString(r.error.message));
                return;
            }
            simdjson::dom::parser parser;
            auto rootResult = parser.parse(r.data);
            if (rootResult.error() != simdjson::SUCCESS) {
                guard->statusLabel_->setText("Failed to parse thread replies.");
                return;
            }

            // The /relations response includes the original event under "original_event"
            // and the related events under "chunk". Insert the root first.
            auto origResult = rootResult.value()["original_event"];
            if (origResult.error() == simdjson::SUCCESS) {
                DisplayedEvent root;
                auto t = origResult.value()["type"].get_string();
                if (t.error() == simdjson::SUCCESS) root.type = std::string(t.value());
                auto eid = origResult.value()["event_id"].get_string();
                if (eid.error() == simdjson::SUCCESS) root.eventId = std::string(eid.value());
                auto sender = origResult.value()["sender"].get_string();
                if (sender.error() == simdjson::SUCCESS) root.senderId = std::string(sender.value());
                auto ts = origResult.value()["origin_server_ts"].get_int64();
                if (ts.error() == simdjson::SUCCESS) root.originServerTs = ts.value();
                auto contentResult = origResult.value()["content"];
                if (contentResult.error() == simdjson::SUCCESS) {
                    root.contentJson = simdjson::to_string(contentResult.value());
                }
                if (!root.senderId.empty() && root.senderId[0] == '@') {
                    auto colon = root.senderId.find(':');
                    if (colon != std::string::npos) root.senderName = root.senderId.substr(1, colon - 1);
                    else root.senderName = root.senderId.substr(1);
                }
                if (root.type == "m.room.message") {
                    root.msgtype = extractMsgtype(root.contentJson);
                    root.body = extractBody(root.contentJson);
                    if (root.msgtype == "m.image" || root.msgtype == "m.video") {
                        root.mxcUrl = extractMxcUrl(root.contentJson);
                        root.mimetype = extractMimetype(root.contentJson);
                    }
                }
                // Mark as thread root
                root.isThreadRoot = true;
                guard->timelineModel_->appendBack(root);
            }

            auto chunkResult = rootResult.value()["chunk"].get_array();
            if (chunkResult.error() != simdjson::SUCCESS) {
                guard->statusLabel_->setText("No thread replies found.");
                return;
            }
            std::vector<DisplayedEvent> events;
            for (auto evt : chunkResult.value()) {
                DisplayedEvent de;
                auto t = evt["type"].get_string();
                if (t.error() == simdjson::SUCCESS) de.type = std::string(t.value());
                auto eid = evt["event_id"].get_string();
                if (eid.error() == simdjson::SUCCESS) de.eventId = std::string(eid.value());
                auto sender = evt["sender"].get_string();
                if (sender.error() == simdjson::SUCCESS) de.senderId = std::string(sender.value());
                auto ts = evt["origin_server_ts"].get_int64();
                if (ts.error() == simdjson::SUCCESS) de.originServerTs = ts.value();
                auto contentResult = evt["content"];
                if (contentResult.error() == simdjson::SUCCESS) {
                    de.contentJson = simdjson::to_string(contentResult.value());
                }
                if (!de.senderId.empty() && de.senderId[0] == '@') {
                    auto colon = de.senderId.find(':');
                    if (colon != std::string::npos) de.senderName = de.senderId.substr(1, colon - 1);
                    else de.senderName = de.senderId.substr(1);
                }
                if (de.type == "m.room.message") {
                    de.msgtype = extractMsgtype(de.contentJson);
                    de.body = extractBody(de.contentJson);
                    if (de.msgtype == "m.image" || de.msgtype == "m.video") {
                        de.mxcUrl = extractMxcUrl(de.contentJson);
                        de.mimetype = extractMimetype(de.contentJson);
                    }
                }
                de.isThreadReply = true;
                events.push_back(std::move(de));
            }
            for (const auto& de : events) {
                guard->timelineModel_->appendBack(de);
            }
            guard->statusLabel_->setText(QString("Loaded %1 thread reply(s).").arg(events.size()));
        }, Qt::QueuedConnection);
    }).detach();
}

void MainWindow::closeThreadView() {
    currentThreadRoot_.clear();
    threadBanner_->hide();
    timelineModel_->clear();
    if (!currentRoomId_.isEmpty()) {
        roomStore_->loadHistory(currentRoomId_.toStdString(), timelineModel_,
            QPointer<QWidget>(this), [this](int count, const std::string& pb) {
                if (count > 0) { currentPrevBatch_ = pb; if (!pb.empty() && loadMoreBtn_) loadMoreBtn_->show(); timelineView_->scrollToBottom(); }
            });
    }
}

void MainWindow::onLoadMoreClicked() {
    if (currentRoomId_.isEmpty() || !client_ || currentPrevBatch_.empty()) return;
    statusLabel_->setText("Loading older messages...");
    loadMoreBtn_->setEnabled(false);

    std::string roomId = currentRoomId_.toStdString();
    std::string from = currentPrevBatch_;
    MatrixClient* client = client_;
    QPointer<MainWindow> guard(this);

    std::thread([guard, client, roomId, from]() {
        auto result = client->getMessages(roomId, from, 30);
        QMetaObject::invokeMethod(guard, [guard, result]() {
            if (guard.isNull() || !result.ok) {
                if (!guard.isNull()) {
                    guard->statusLabel_->setText("Failed to load older messages.");
                    guard->loadMoreBtn_->setEnabled(true);
                }
                return;
            }

            simdjson::dom::parser parser;
            auto rootResult = parser.parse(result.data);
            if (rootResult.error() != simdjson::SUCCESS) {
                guard->statusLabel_->setText("Failed to parse older messages.");
                guard->loadMoreBtn_->setEnabled(true);
                return;
            }

            auto endToken = rootResult.value()["end"].get_string();
            if (endToken.error() == simdjson::SUCCESS && endToken.value().size() > 0) {
                guard->currentPrevBatch_ = std::string(endToken.value());
            } else {
                auto startToken = rootResult.value()["start"].get_string();
                if (startToken.error() == simdjson::SUCCESS) {
                    guard->currentPrevBatch_ = std::string(startToken.value());
                } else {
                    guard->currentPrevBatch_.clear();
                }
            }

            auto chunkResult = rootResult.value()["chunk"].get_array();
            if (chunkResult.error() != simdjson::SUCCESS) {
                guard->statusLabel_->setText("No older messages.");
                guard->loadMoreBtn_->setEnabled(true);
                return;
            }

            std::vector<DisplayedEvent> events;
            for (auto evt : chunkResult.value()) {
                DisplayedEvent de;
                auto t = evt["type"].get_string();
                if (t.error() == simdjson::SUCCESS) de.type = std::string(t.value());
                auto eid = evt["event_id"].get_string();
                if (eid.error() == simdjson::SUCCESS) de.eventId = std::string(eid.value());
                auto sender = evt["sender"].get_string();
                if (sender.error() == simdjson::SUCCESS) de.senderId = std::string(sender.value());
                auto ts = evt["origin_server_ts"].get_int64();
                if (ts.error() == simdjson::SUCCESS) de.originServerTs = ts.value();
                auto contentResult = evt["content"];
                if (contentResult.error() == simdjson::SUCCESS) de.contentJson = simdjson::to_string(contentResult.value());

                if (!de.senderId.empty() && de.senderId[0] == '@') {
                    auto colon = de.senderId.find(':');
                    if (colon != std::string::npos) de.senderName = de.senderId.substr(1, colon - 1);
                    else de.senderName = de.senderId.substr(1);
                }
                if (de.type == "m.room.message") {
                    de.msgtype = extractMsgtype(de.contentJson);
                    de.body = extractBody(de.contentJson);
                    if (de.msgtype == "m.image" || de.msgtype == "m.video") {
                        de.mxcUrl = extractMxcUrl(de.contentJson);
                        de.mimetype = extractMimetype(de.contentJson);
                    }
                    std::string_view cv(de.contentJson);
                    std::string threadRoot = extractThreadRootId(cv);
                    if (!threadRoot.empty()) {
                        de.isThreadReply = true;
                        de.threadRootId = threadRoot;
                    }
                    std::string replyTo = extractReplyToId(cv);
                    if (!replyTo.empty()) {
                        de.isReply = true;
                        de.replyToEventId = replyTo;
                    }
                }
                // Set avatars from cache
                auto it = guard->memberAvatarCache_.find(de.senderId);
                if (it != guard->memberAvatarCache_.end()) de.avatarUrl = it->second;
                auto nameIt = guard->memberAvatarCache_.find(de.senderId + "/name");
                if (nameIt != guard->memberAvatarCache_.end()) de.senderName = nameIt->second;

                events.push_back(std::move(de));
            }

            std::reverse(events.begin(), events.end());
            guard->timelineModel_->appendFront(events);

            if (guard->currentPrevBatch_.empty()) {
                guard->loadMoreBtn_->hide();
            }
            guard->loadMoreBtn_->setEnabled(true);
            guard->statusLabel_->setText(QString("Loaded %1 older message(s).").arg(events.size()));
        }, Qt::QueuedConnection);
    }).detach();
}

void MainWindow::onToggleChatLog() {
    if (currentRoomId_.isEmpty()) return;
    chatLogging_ = !chatLogging_;

    if (chatLogging_) {
        chatLogBtn_->setChecked(true);
        chatLogBtn_->setText("💾 Saving");
        // Open file for appending
        QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/chatlogs";
        QDir().mkpath(dir);
        QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd_HHmm");
        // Get room name
        std::string roomName = currentRoomId_.toStdString();
        int row = roomModel_->findRowByRoomId(roomName);
        if (row >= 0) {
            auto* rd = roomModel_->at(row);
            if (rd && !rd->name.empty()) roomName = rd->name;
        }
        // Sanitize filename
        for (auto& c : roomName) {
            if (c == '/' || c == '\\' || c == ':' || c == '<' || c == '>' || c == '|' || c == '?') c = '_';
        }
        QString filePath = dir + "/" + QString::fromStdString(roomName) + "_" + timestamp + ".txt";
        chatLogFile_ = std::make_unique<std::ofstream>(filePath.toStdString(), std::ios::app);
        if (chatLogFile_->is_open()) {
            *chatLogFile_ << "=== Chat log: " << roomName << " ===\n"
                         << "Started: " << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss").toStdString() << "\n\n";
            statusLabel_->setText("Chat log started: " + filePath);
        } else {
            chatLogging_ = false;
            chatLogBtn_->setChecked(false);
            chatLogBtn_->setText("💾 Save");
            statusLabel_->setText("Failed to create log file.");
        }
    } else {
        chatLogBtn_->setChecked(false);
        chatLogBtn_->setText("💾 Save");
        chatLogFile_.reset();
        statusLabel_->setText("Chat log stopped.");
    }
}

void MainWindow::toggleThreadPanel() {
    if (currentRoomId_.isEmpty() || !client_) {
        QMessageBox::information(this, "Threads", "Select a room first.");
        return;
    }
    ThreadsDialog dlg(client_, currentRoomId_.toStdString(), this);
    dlg.exec();
}

void MainWindow::onSync(FastSyncResponse resp) {
    bool hasData = !resp.joinedRooms.empty() || !resp.leftRoomIds.empty()
                   || !resp.invitedRooms.empty();

    if (hasData && roomStore_) {
        statusLabel_->setText("Syncing...");
        QPointer<MainWindow> guard(this);
        std::string myUserId = client_ ? client_->account().userId : "";
        std::string curRoomId = currentRoomId_.toStdString();

        std::thread([guard, resp = std::move(resp), myUserId, curRoomId]() mutable {
            auto syncUpdate = RoomStore::prepareRoomSyncUpdate(resp, curRoomId, myUserId);

            QMetaObject::invokeMethod(guard, [guard, syncUpdate = std::move(syncUpdate)]() mutable {
                if (guard.isNull()) return;
                guard->roomStore_->applyRoomSyncUpdate(syncUpdate,
                    guard->roomModel_, guard->timelineModel_);

                // Bug D: clear timeline if current room was left/kicked
                for (const auto& rid : syncUpdate.roomsToRemove) {
                    if (rid == guard->currentRoomId_.toStdString()) {
                        guard->timelineModel_->clear();
                        guard->currentRoomId_.clear();
                        guard->timelineView_->hide();
                        guard->timelinePlaceholder_->show();
                        guard->messageEdit_->hide();
                        if (guard->loadMoreBtn_) guard->loadMoreBtn_->hide();
                        break;
                    }
                }

                // Widget updates
                if (syncUpdate.inviteCount > 0) {
                    guard->inviteHeader_->setText(syncUpdate.inviteText);
                    guard->inviteHeader_->show();
                } else {
                    guard->inviteHeader_->hide();
                }
                guard->updateRoomListHeader();
                logMemorySnapshot("after-rebuildRoomList");

                // Bug C: show last message body in notification
                static bool firstNotify = true;
                if (!firstNotify) {
                    for (auto& rd : syncUpdate.roomsToUpsert) {
                        if (rd.highlightCount > 0) {
                            QString body = syncUpdate.lastNotificationBody.empty()
                                ? QString("Highlight!") : QString::fromStdString(syncUpdate.lastNotificationBody);
                            guard->notifier_.notify(QString::fromStdString(rd.name), body);
                            break;
                        }
                    }
                }
                firstNotify = false;
                guard->roomStore_->batchLoadRoomStates(guard->roomModel_, QPointer<QWidget>(guard));

                // Bug A: update status with real count
                guard->statusLabel_->setText(QString("Synced: %1 rooms | %2 messages")
                    .arg(guard->roomModel_->rowCount()).arg(guard->timelineModel_->rowCount()));

                // Bug B: trim + log after buffer released (worker thread finished)
                logMemorySnapshot("after-sync-cleanup");
                trimMemory();
            }, Qt::QueuedConnection);
        }).detach();
    }

    // Memory snapshot after receiving sync (before processing)
    static bool firstSync = true;
    if (firstSync) logMemorySnapshot("after-first-sync");
    firstSync = false;
}

void MainWindow::onSyncState(SyncEngineState state, const SyncEngineStats& stats) {
    const char* s = "?";
    switch (state) {
        case SyncEngineState::Stopped:     s = "stopped"; break;
        case SyncEngineState::InitialSync: s = "downloading chats..."; break;
        case SyncEngineState::Running:    s = "synced"; break;
        case SyncEngineState::Backoff:     s = "reconnecting..."; break;
        case SyncEngineState::Paused:      s = "paused"; break;
    }
    statusLabel_->setText(QString("%1 | %2 chats / %3 events").arg(s).arg(stats.roomsJoined).arg(stats.timelineEvents));
}

void MainWindow::updateRoomListHeader() {
    int totalRooms = roomModel_->rowCount();
    int inviteCount = 0;
    int joinedCount = 0;
    for (int i = 0; i < totalRooms; ++i) {
        auto* rd = roomModel_->at(i);
        if (rd) {
            if (rd->isInvite) inviteCount++;
            else joinedCount++;
        }
    }
    roomListHeader_->setText(QString(" Chats (%1) ").arg(joinedCount));
    if (inviteCount > 0) {
        inviteHeader_->setText(QString(" ⬇ Invitations (%1) ").arg(inviteCount));
        inviteHeader_->show();
    } else {
        inviteHeader_->hide();
    }
}


} // namespace progressive::desktop
