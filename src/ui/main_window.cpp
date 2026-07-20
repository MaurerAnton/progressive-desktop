// src/ui/main_window.cpp — Phase 3 full UI.
#include "main_window.hpp"
#include "login_dialog.hpp"
#include "image_viewer_dialog.hpp"
#include "room_directory_dialog.hpp"
#include "threads_dialog.hpp"
#include "emoji_picker.hpp"
#include "room_settings_dialog.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
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

#include <progressive/markdown.hpp>
#include <progressive/json_parser.hpp>
#include <progressive/event_models.hpp>
#include <progressive/permalink.hpp>

#include <simdjson.h>

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

// Compute room name from state events AND timeline events.
// Returns EMPTY STRING if no name found — caller should keep old name.
std::string computeRoomName(const std::vector<FastEvent>& stateEvents,
                              const std::vector<FastEvent>& timelineEvents,
                              const std::string& roomId) {
    // Debug: log what state events we have for this room
    if (getenv("PD_DEBUG_ROOMS")) {
        std::fprintf(stderr, "[PD_DEBUG] room %s: %zu state events, %zu timeline events\n",
                     roomId.c_str(), stateEvents.size(), timelineEvents.size());
        for (const auto& e : stateEvents) {
            if (e.type == "m.room.name" || e.type == "m.room.canonical_alias" || e.type == "m.room.avatar") {
                std::fprintf(stderr, "[PD_DEBUG]   state: type=%.*s contentJson=%.*s\n",
                             (int)e.type.size(), e.type.data(),
                             (int)e.contentJson.size(), e.contentJson.data());
            }
        }
    }

    // 1. m.room.name in state events
    for (const auto& e : stateEvents) {
        if (e.type == "m.room.name" && !e.contentJson.empty()) {
            auto name = extractJsonString(e.contentJson, "name");
            if (!name.empty()) return name;
        }
    }
    // 2. m.room.name in timeline events (initial sync often puts state here)
    for (const auto& e : timelineEvents) {
        if (e.type == "m.room.name" && !e.contentJson.empty()) {
            auto name = extractJsonString(e.contentJson, "name");
            if (!name.empty()) return name;
        }
    }
    // 3. m.room.canonical_alias in state events
    for (const auto& e : stateEvents) {
        if (e.type == "m.room.canonical_alias" && !e.contentJson.empty()) {
            auto alias = extractJsonString(e.contentJson, "alias");
            if (!alias.empty()) return alias;
        }
    }
    // 4. m.room.canonical_alias in timeline events
    for (const auto& e : timelineEvents) {
        if (e.type == "m.room.canonical_alias" && !e.contentJson.empty()) {
            auto alias = extractJsonString(e.contentJson, "alias");
            if (!alias.empty()) return alias;
        }
    }
    // 5. Members: collect displaynames from m.room.member (joined)
    std::vector<std::string> members;
    for (const auto& e : stateEvents) {
        if (e.type == "m.room.member" && !e.contentJson.empty()) {
            auto membership = extractJsonString(e.contentJson, "membership");
            if (membership != "join") continue;
            auto displayname = extractJsonString(e.contentJson, "displayname");
            if (!displayname.empty()) {
                members.push_back(displayname);
                continue;
            }
            if (!e.senderId.empty()) {
                std::string_view uid = e.senderId;
                if (uid[0] == '@') {
                    auto colon = uid.find(':');
                    if (colon != std::string::npos) members.push_back(std::string(uid.substr(1, colon - 1)));
                    else members.push_back(std::string(uid.substr(1)));
                }
            }
        }
    }
    // Also check timeline for member events
    for (const auto& e : timelineEvents) {
        if (e.type == "m.room.member" && !e.contentJson.empty()) {
            auto membership = extractJsonString(e.contentJson, "membership");
            if (membership != "join") continue;
            auto displayname = extractJsonString(e.contentJson, "displayname");
            if (!displayname.empty()) {
                members.push_back(displayname);
                continue;
            }
            if (!e.senderId.empty()) {
                std::string_view uid = e.senderId;
                if (uid[0] == '@') {
                    auto colon = uid.find(':');
                    if (colon != std::string::npos) members.push_back(std::string(uid.substr(1, colon - 1)));
                    else members.push_back(std::string(uid.substr(1)));
                }
            }
        }
    }
    std::sort(members.begin(), members.end());
    members.erase(std::unique(members.begin(), members.end()), members.end());
    if (members.size() == 1) return members[0];
    if (members.size() == 2) return members[0] + ", " + members[1];
    if (members.size() > 2) return members[0] + " + " + std::to_string(members.size() - 1);
    // 6. No name found — return empty so caller keeps old name
    return {};
}

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
    settingsAction_ = toolbar_->addAction("Settings");
    toolbar_->addSeparator();
    fullscreenAction_ = toolbar_->addAction("Fullscreen");

    auto* spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar_->addWidget(spacer);
    logoutAction_ = toolbar_->addAction("Logout");

    connect(logoutAction_, &QAction::triggered, this, &MainWindow::onLogoutClicked);
    connect(newChatAction_, &QAction::triggered, this, &MainWindow::onNewChatClicked);
    connect(joinRoomAction_, &QAction::triggered, this, &MainWindow::onJoinRoomClicked);
    connect(browseRoomsAction_, &QAction::triggered, this, &MainWindow::onBrowseRoomsClicked);
    connect(allThreadsAction_, &QAction::triggered, this, &MainWindow::onAllThreadsClicked);
    connect(roomSettingsAction_, &QAction::triggered, this, &MainWindow::onRoomSettingsClicked);
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

    roomList_ = new QListView(leftPanel);
    roomList_->setModel(roomModel_ = new RoomListModel(roomList_));
    roomList_->setMinimumWidth(280);
    roomList_->setMaximumWidth(400);
    roomList_->setAlternatingRowColors(true);
    roomList_->setWordWrap(true);
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
    rightLayout->addWidget(timelineView_);

    timelinePlaceholder_ = new QLabel(
        "Select a chat from the list\nor click \"+ New chat\" to start a conversation", this);
    timelinePlaceholder_->setAlignment(Qt::AlignCenter);
    timelinePlaceholder_->setStyleSheet("color:#969696; font-size:14pt; background:#141414;");
    rightLayout->addWidget(timelinePlaceholder_);
    timelinePlaceholder_->show();
    timelineView_->hide();

    messageEdit_ = new MessageEdit(rightPanel);
    messageEdit_->hide();
    rightLayout->addWidget(messageEdit_);

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
    connect(messageEdit_, &MessageEdit::sendMessage, this, &MainWindow::onSendMessage);
    connect(messageEdit_, &MessageEdit::slashCommand, this, &MainWindow::onSlashCommand);

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
    sync_.onSync([this](const FastSyncResponse& resp) {
        QMetaObject::invokeMethod(this, [this, resp]() { onSync(resp); }, Qt::QueuedConnection);
    });
    sync_.onStateChange([this](SyncEngineState st, const SyncEngineStats& stats) {
        QMetaObject::invokeMethod(this, [this, st, stats]() { onSyncState(st, stats); }, Qt::QueuedConnection);
    });
    sync_.onAuthError([this]() {
        QMetaObject::invokeMethod(this, [this]() { forceReLogin(); }, Qt::QueuedConnection);
    });
}

void MainWindow::startWithSavedSession() {
    if (!client_ || !client_->isLoggedIn()) return;
    // Set client on image loader
    imageLoader_->setClient(client_);  // Note: need to add setClient method
    userLabel_->setText(" " + QString::fromStdString(client_->account().userId) + " ");
    statusLabel_->setText("Starting sync...");
    sync_.setClient(client_);
    sync_.setSessionStore(store_);
    sync_.start();
}

void MainWindow::closeEvent(QCloseEvent* e) { sync_.stop(); e->accept(); }

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
                    // Remove from model IMMEDIATELY — don't wait for next sync.
                    // The next /sync will also include it in rooms.leave but
                    // rebuildRoomList will just no-op (room already removed).
                    guard->roomModel_->removeRoom(roomId);
                    if (guard->currentRoomId_.toStdString() == roomId) {
                        guard->currentRoomId_.clear();
                        guard->timelineModel_->clear();
                        guard->timelineView_->hide();
                        guard->timelinePlaceholder_->show();
                        guard->messageEdit_->hide();
                    }
                } else {
                    guard->statusLabel_->setText("Failed to leave: " + QString::fromStdString(r.error.message));
                }
            }, Qt::QueuedConnection);
        }).detach();
    }
}

void MainWindow::onRoomClicked(const QModelIndex& idx) {
    const RoomData* r = roomModel_->at(idx.row());
    if (!r) return;
    currentRoomId_ = QString::fromStdString(r->roomId);
    timelineModel_->clear();
    timelinePlaceholder_->hide();
    timelineView_->show();
    messageEdit_->show();
    messageEdit_->setFocus();
    setWindowTitle(QString("Progressive Chat — %1").arg(QString::fromStdString(r->name)));

    // Load room history via /messages endpoint
    loadRoomHistory(r->roomId);
}

void MainWindow::loadRoomHistory(const std::string& roomId) {
    if (!client_ || !client_->isLoggedIn()) return;

    statusLabel_->setText("Loading history...");
    MatrixClient* client = client_;
    QPointer<MainWindow> guard(this);

    std::thread([guard, client, roomId]() {
        auto result = client->getMessages(roomId, "", 30);

        QMetaObject::invokeMethod(guard, [guard, result, roomId]() {
            if (guard.isNull()) return;

            if (!result.ok) {
                guard->statusLabel_->setText("Failed to load history: " +
                    QString::fromStdString(result.error.message));
                return;
            }

            // Parse /messages response with simdjson.
            // Build DisplayedEvent DIRECTLY (not FastEvent which uses
            // string_view — would dangle after parser destroys the buffer).
            simdjson::dom::parser parser;
            auto rootResult = parser.parse(result.data);
            if (rootResult.error() != simdjson::SUCCESS) {
                guard->statusLabel_->setText("Failed to parse history.");
                return;
            }

            auto chunkResult = rootResult.value()["chunk"].get_array();
            if (chunkResult.error() != simdjson::SUCCESS) {
                guard->statusLabel_->setText("No history found.");
                return;
            }

            std::vector<DisplayedEvent> events;
            for (auto evt : chunkResult.value()) {
                DisplayedEvent de;
                // Extract fields as OWNED std::string (parser is local,
                // will be destroyed when this lambda returns)
                auto t = evt["type"].get_string();
                if (t.error() == simdjson::SUCCESS) de.type = std::string(t.value());

                auto eid = evt["event_id"].get_string();
                if (eid.error() == simdjson::SUCCESS) de.eventId = std::string(eid.value());

                auto sender = evt["sender"].get_string();
                if (sender.error() == simdjson::SUCCESS) de.senderId = std::string(sender.value());

                auto ts = evt["origin_server_ts"].get_int64();
                if (ts.error() == simdjson::SUCCESS) de.originServerTs = ts.value();

                // Serialize content object — owned std::string
                auto contentResult = evt["content"];
                if (contentResult.error() == simdjson::SUCCESS) {
                    de.contentJson = simdjson::to_string(contentResult.value());
                }

                // Extract senderName (localpart of senderId)
                if (!de.senderId.empty() && de.senderId[0] == '@') {
                    auto colon = de.senderId.find(':');
                    if (colon != std::string::npos) de.senderName = de.senderId.substr(1, colon - 1);
                    else de.senderName = de.senderId.substr(1);
                }

                // For m.room.message, extract msgtype/body/mxcUrl/mimetype
                if (de.type == "m.room.message") {
                    de.msgtype = extractMsgtype(de.contentJson);
                    de.body = extractBody(de.contentJson);
                    if (de.msgtype == "m.image" || de.msgtype == "m.video") {
                        de.mxcUrl = extractMxcUrl(de.contentJson);
                        de.mimetype = extractMimetype(de.contentJson);
                        if (de.mimetype == "image/gif") de.isMovie = true;
                    }
                }

                events.push_back(std::move(de));
            }

            // Events from /messages?dir=b are newest-first. Reverse to oldest-first.
            std::reverse(events.begin(), events.end());

            // Append directly to timeline model
            for (const auto& de : events) {
                guard->timelineModel_->appendBack(de);
            }
            guard->statusLabel_->setText(QString("Loaded %1 message(s) from history.").arg(events.size()));
        }, Qt::QueuedConnection);
    }).detach();
}

void MainWindow::onSendMessage(const std::string& body) {
    if (currentRoomId_.isEmpty() || !client_) return;
    // Local echo
    DisplayedEvent echo;
    echo.eventId = "";
    echo.senderId = client_->account().userId;
    echo.senderName = "you";
    echo.type = "m.room.message";
    echo.msgtype = "m.text";
    echo.body = body;
    echo.originServerTs = static_cast<int64_t>(QDateTime::currentMSecsSinceEpoch());
    timelineModel_->appendBack(echo);

    std::string roomId = currentRoomId_.toStdString();
    MatrixClient* client = client_;
    std::thread([client, roomId, body]() {
        auto r = client->sendMessage(roomId, body);
        if (!r.ok) std::cerr << "send failed: " << r.error.message << "\n";
    }).detach();
}

void MainWindow::onSlashCommand(const std::string& cmd, const std::string& args) {
    if (cmd == "help") {
        // Append as a system message
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
    } else if (cmd == "me") {
        if (currentRoomId_.isEmpty() || !client_) return;
        DisplayedEvent echo;
        echo.senderId = client_->account().userId;
        echo.senderName = "you";
        echo.type = "m.room.message";
        echo.msgtype = "m.emote";
        echo.body = args;
        echo.originServerTs = static_cast<int64_t>(QDateTime::currentMSecsSinceEpoch());
        timelineModel_->appendBack(echo);
        std::string roomId = currentRoomId_.toStdString();
        MatrixClient* client = client_;
        std::thread([client, roomId, body = args]() { client->sendMessage(roomId, body, "m.emote"); }).detach();
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
    if (store_) store_->clearAccount();
    if (client_) { AccountInfo empty; client_->setAccount(empty); }
    roomModel_->clear();
    timelineModel_->clear();
    timelineView_->hide();
    timelinePlaceholder_->show();
    messageEdit_->hide();
    currentRoomId_.clear();
    userLabel_->setText(" Not logged in ");
    statusLabel_->setText("Session expired. Please login again.");
    showLoginDialog();
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
        statusLabel_->setText("Joined room: " + dlg.joinedRoomId());
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

void MainWindow::onSettingsClicked() {
    QMessageBox::information(this, "Settings",
        "Progressive Chat — Desktop\n\nVersion: 0.0.4\nPhase: 3 (full UI)\n\n"
        "Toolbar buttons:\n"
        "  + New chat — start a DM\n"
        "  Join by ID — join room by ID/alias\n"
        "  Browse rooms — search public rooms\n"
        "  All threads — view threads in current room\n"
        "  Room settings — topic, name, members, kick/ban/promote\n"
        "  Fullscreen — toggle fullscreen (F11)\n"
        "  Logout — sign out\n\n"
        "In chat:\n"
        "  Right-click message — react, pin, copy link, redact\n"
        "  Click image — zoom + open externally\n\n"
        "Slash commands: /help /clear /logout /me <action>");
}

void MainWindow::onImageClicked(const QString& eventId, const QString& mxcUrl) {
    if (mxcUrl.isEmpty()) return;
    std::string mxc = mxcUrl.toStdString();
    MatrixClient* client = client_;
    QPointer<MainWindow> guard(this);

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
    // Could open thread view, reply, etc. For now, do nothing.
    (void)eventId;
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
    menu.addSeparator();
    auto* pinAction = menu.addAction("Pin message");
    auto* copyLinkAction = menu.addAction("Copy permalink");
    menu.addSeparator();
    auto* redactAction = menu.addAction("Redact (delete)");

    auto* selected = menu.exec(globalPos);
    if (!selected) return;

    if (selected == reactAction) {
        EmojiPicker picker(this);
        connect(&picker, &EmojiPicker::emojiSelected, this, [this, eventId](const QString& emoji) {
            if (currentRoomId_.isEmpty() || !client_) return;
            std::string roomId = currentRoomId_.toStdString();
            std::string eid = eventId.toStdString();
            std::string em = emoji.toStdString();
            MatrixClient* client = client_;
            std::thread([client, roomId, eid, em]() {
                client->sendReaction(roomId, eid, em);
            }).detach();
            timelineModel_->addReaction(eventId.toStdString(), emoji.toStdString(),
                                         client_->account().userId);
        });
        picker.exec();
    } else if (selected == pinAction) {
        if (currentRoomId_.isEmpty() || !client_) return;
        std::string roomId = currentRoomId_.toStdString();
        std::string eid = eventId.toStdString();
        MatrixClient* client = client_;
        QPointer<MainWindow> guard(this);
        std::thread([guard, client, roomId, eid]() {
            auto r = client->pinMessage(roomId, eid);
            QMetaObject::invokeMethod(guard, [guard, r, eid]() {
                if (guard.isNull()) return;
                if (r.ok) { guard->timelineModel_->setPinned(eid, true);
                            guard->statusLabel_->setText("Message pinned."); }
                else guard->statusLabel_->setText("Pin failed: " + QString::fromStdString(r.error.message));
            }, Qt::QueuedConnection);
        }).detach();
    } else if (selected == copyLinkAction) {
        // Build matrix.to permalink
        std::string roomId = currentRoomId_.toStdString();
        std::string eid = eventId.toStdString();
        std::string permalink = "https://matrix.to/#/" + roomId + "/" + eid;
        QGuiApplication::clipboard()->setText(QString::fromStdString(permalink));
        statusLabel_->setText("Permalink copied to clipboard.");
    } else if (selected == redactAction) {
        auto reply = QMessageBox::question(this, "Redact",
            "Delete this message? This cannot be undone.",
            QMessageBox::Yes | QMessageBox::No);
        if (reply != QMessageBox::Yes) return;
        std::string roomId = currentRoomId_.toStdString();
        std::string eid = eventId.toStdString();
        MatrixClient* client = client_;
        QPointer<MainWindow> guard(this);
        std::thread([guard, client, roomId, eid]() {
            auto r = client->redactEvent(roomId, eid);
            QMetaObject::invokeMethod(guard, [guard, r]() {
                if (guard.isNull()) return;
                if (r.ok) guard->statusLabel_->setText("Message redacted.");
                else guard->statusLabel_->setText("Redact failed: " + QString::fromStdString(r.error.message));
            }, Qt::QueuedConnection);
        }).detach();
    }
}

void MainWindow::onSync(const FastSyncResponse& resp) {
    rebuildRoomList(resp);
    statusLabel_->setText(QString("synced: %1 chat(s), %2 event(s)")
        .arg(resp.joinedRooms.size()).arg(sync_.stats().timelineEvents));
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
    roomListHeader_->setText(QString(" Chats (%1) ").arg(roomModel_->rowCount()));
}

DisplayedEvent MainWindow::fastEventToDisplayed(const FastEvent& fe) {
    DisplayedEvent de;
    de.eventId = std::string(fe.eventId);
    de.senderId = std::string(fe.senderId);
    de.type = std::string(fe.type);
    de.contentJson = std::string(fe.contentJson);
    de.originServerTs = fe.originServerTs;

    // Extract senderName (localpart of senderId)
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
            if (de.mimetype == "image/gif") de.isMovie = true;
        }
    }
    return de;
}

void MainWindow::rebuildRoomList(const FastSyncResponse& resp) {
    // Remove rooms we've left (or been kicked from) since the last sync.
    // The server includes them in rooms.leave ONE TIME so we can clean up.
    for (const auto& leftId : resp.leftRoomIds) {
        std::string roomId(leftId);
        roomModel_->removeRoom(roomId);
        // If it was the current room, clear the timeline view too
        if (currentRoomId_.toStdString() == roomId) {
            currentRoomId_.clear();
            timelineModel_->clear();
            timelineView_->hide();
            timelinePlaceholder_->show();
            messageEdit_->hide();
        }
    }

    for (const auto& [roomIdView, room] : resp.joinedRooms) {
        std::string roomId(roomIdView);
        RoomData rd;
        rd.roomId = roomId;

        // Compute name from BOTH state and timeline events.
        // Returns empty if no name found — then we keep the old name.
        std::string name = computeRoomName(room.stateEvents, room.timeline.events, roomId);
        if (!name.empty()) {
            rd.name = name;
        } else {
            // No name found in this sync — check if we already have a name.
            int existingRow = roomModel_->findRowByRoomId(roomId);
            if (existingRow >= 0) {
                const RoomData* existing = roomModel_->at(existingRow);
                if (existing && !existing->name.empty() && existing->name != roomId) {
                    rd.name = existing->name;  // keep old name
                } else {
                    rd.name = roomId;  // first time seeing this room, fallback to ID
                }
            } else {
                rd.name = roomId;  // new room, fallback to ID
            }
        }

        rd.lastMessage = jsonUnescape(extractLastMessageBody(room.timeline.events));
        rd.lastActivityTs = room.timeline.events.empty() ? 0 : room.timeline.events.back().originServerTs;
        if (rd.lastActivityTs == 0 && !room.timeline.events.empty())
            rd.lastActivityTs = room.timeline.events.front().originServerTs;
        rd.unreadCount = room.notificationCount;
        rd.highlightCount = room.highlightCount;
        rd.isEncrypted = room.isEncrypted;

        // Extract room avatar from m.room.avatar state event
        for (const auto& e : room.stateEvents) {
            if (e.type == "m.room.avatar" && !e.contentJson.empty()) {
                rd.avatarUrl = extractJsonStringDecoded(e.contentJson, "url");
                if (!rd.avatarUrl.empty()) break;
            }
        }
        // Also check timeline events for avatar (initial sync)
        if (rd.avatarUrl.empty()) {
            for (const auto& e : room.timeline.events) {
                if (e.type == "m.room.avatar" && !e.contentJson.empty()) {
                    rd.avatarUrl = extractJsonStringDecoded(e.contentJson, "url");
                    if (!rd.avatarUrl.empty()) break;
                }
            }
        }

        roomModel_->upsertRoom(rd);

        if (currentRoomId_.toStdString() == roomId) {
            appendTimelineForRoom(roomId, room.timeline.events);
        }
    }
    updateRoomListHeader();
}

void MainWindow::appendTimelineForRoom(const std::string& roomId, const std::vector<FastEvent>& events) {
    for (const auto& e : events) {
        timelineModel_->appendBack(fastEventToDisplayed(e));
    }
}

} // namespace progressive::desktop
