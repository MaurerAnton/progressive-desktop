// src/ui/main_window.cpp — Phase 2 main window with full UI.

#include "main_window.hpp"
#include "login_dialog.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenuBar>
#include <QMessageBox>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QAction>
#include <QUrl>
#include <QVBoxLayout>
#include <QStandardPaths>
#include <QDir>

#include <progressive/markdown.hpp>
#include <progressive/json_parser.hpp>
#include <progressive/event_models.hpp>

#include <iostream>
#include <chrono>
#include <thread>

namespace progressive::desktop {

namespace {

// Try to compute a human-readable room name from state events.
// Priority: m.room.name → m.room.canonical_alias → members (Alice, Bob) → "Без названия"
std::string computeRoomName(const std::vector<FastEvent>& stateEvents,
                              const std::string& roomId) {
    // 1. m.room.name
    for (const auto& e : stateEvents) {
        if (e.type == "m.room.name" && !e.contentJson.empty()) {
            auto pos = e.contentJson.find("\"name\":\"");
            if (pos != std::string::npos) {
                pos += 7;
                auto start = e.contentJson.find('"', pos) + 1;
                auto end = e.contentJson.find('"', start);
                if (end != std::string::npos && start < end) {
                    auto name = e.contentJson.substr(start, end - start);
                    if (!name.empty()) return std::string(name);
                }
            }
        }
    }

    // 2. m.room.canonical_alias
    for (const auto& e : stateEvents) {
        if (e.type == "m.room.canonical_alias" && !e.contentJson.empty()) {
            auto pos = e.contentJson.find("\"alias\":\"");
            if (pos != std::string::npos) {
                pos += 8;
                auto start = e.contentJson.find('"', pos) + 1;
                auto end = e.contentJson.find('"', start);
                if (end != std::string::npos && start < end) {
                    return std::string(e.contentJson.substr(start, end - start));
                }
            }
        }
    }

    // 3. Members: collect displaynames/userIds from m.room.member (joined)
    std::vector<std::string> members;
    for (const auto& e : stateEvents) {
        if (e.type == "m.room.member" && !e.contentJson.empty()) {
            // Check membership == "join"
            auto mpos = e.contentJson.find("\"membership\":\"");
            std::string_view membership;
            if (mpos != std::string::npos) {
                mpos += 13;
                auto mend = e.contentJson.find('"', mpos);
                if (mend != std::string::npos) {
                    membership = e.contentJson.substr(mpos, mend - mpos);
                }
            }
            if (membership != "join") continue;

            // Try displayname
            auto dpos = e.contentJson.find("\"displayname\":\"");
            if (dpos != std::string::npos) {
                dpos += 14;
                auto dstart = e.contentJson.find('"', dpos) + 1;
                auto dend = e.contentJson.find('"', dstart);
                if (dend != std::string::npos && dstart < dend) {
                    members.push_back(std::string(e.contentJson.substr(dstart, dend - dstart)));
                    continue;
                }
            }
            // Fallback: userId (sender)
            if (!e.senderId.empty()) {
                std::string_view uid = e.senderId;
                if (uid[0] == '@') {
                    auto colon = uid.find(':');
                    if (colon != std::string::npos) {
                        members.push_back(std::string(uid.substr(1, colon - 1)));
                    } else {
                        members.push_back(std::string(uid.substr(1)));
                    }
                }
            }
        }
    }

    // Remove duplicates
    std::sort(members.begin(), members.end());
    members.erase(std::unique(members.begin(), members.end()), members.end());

    if (members.size() == 1) {
        return members[0];
    } else if (members.size() == 2) {
        return members[0] + ", " + members[1];
    } else if (members.size() > 2) {
        return members[0] + " + " + std::to_string(members.size() - 1) + " others";
    }

    // 4. Fallback
    return "Без названия";
}

// Extract last text message body from timeline events.
std::string_view extractLastMessageBody(const std::vector<FastEvent>& events) {
    for (auto it = events.rbegin(); it != events.rend(); ++it) {
        const auto& e = *it;
        if (e.type == "m.room.message" && !e.contentJson.empty()) {
            std::string_view key = "\"body\":\"";
            auto pos = e.contentJson.find(key);
            if (pos != std::string_view::npos) {
                pos += key.size();
                auto end = e.contentJson.find('"', pos);
                if (end != std::string_view::npos) {
                    return e.contentJson.substr(pos, end - pos);
                }
            }
        }
    }
    return {};
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {

    setWindowTitle("Progressive Chat — Desktop");
    resize(1100, 720);

    // --- Toolbar ---
    toolbar_ = addToolBar("Main");
    toolbar_->setMovable(false);

    userLabel_ = new QLabel(" Not logged in ", this);
    toolbar_->addWidget(userLabel_);
    toolbar_->addSeparator();

    newChatAction_ = toolbar_->addAction("+ New chat");
    joinRoomAction_ = toolbar_->addAction("Join room");
    settingsAction_ = toolbar_->addAction("Settings");
    toolbar_->addSeparator();
    fullscreenAction_ = toolbar_->addAction("Fullscreen");
    toolbar_->addSeparator();

    // Spacer
    auto* spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar_->addWidget(spacer);

    logoutAction_ = toolbar_->addAction("Logout");

    connect(logoutAction_, &QAction::triggered, this, &MainWindow::onLogoutClicked);
    connect(newChatAction_, &QAction::triggered, this, &MainWindow::onNewChatClicked);
    connect(joinRoomAction_, &QAction::triggered, this, &MainWindow::onJoinRoomClicked);
    connect(settingsAction_, &QAction::triggered, this, &MainWindow::onSettingsClicked);
    connect(fullscreenAction_, &QAction::triggered, this, &MainWindow::onToggleFullscreen);

    // --- Central splitter ---
    splitter_ = new QSplitter(Qt::Horizontal, this);

    // Left: room list header + list
    auto* leftPanel = new QWidget(splitter_);
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);

    roomListHeader_ = new QLabel(" Chats (0) ", this);
    roomListHeader_->setStyleSheet(
        "font-weight:600; padding:10px 12px; color:#e8e8e8; background:#1e1e1e;");
    leftLayout->addWidget(roomListHeader_);

    roomList_ = new QListView(leftPanel);
    roomList_->setModel(roomModel_ = new RoomListModel(roomList_));
    roomList_->setMinimumWidth(280);
    roomList_->setMaximumWidth(400);
    roomList_->setAlternatingRowColors(true);
    roomList_->setWordWrap(true);
    roomList_->setUniformItemSizes(false);
    leftLayout->addWidget(roomList_);

    // Right: timeline + input
    auto* rightPanel = new QWidget(splitter_);
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);

    timeline_ = new TimelineView(rightPanel);
    rightLayout->addWidget(timeline_);

    timelinePlaceholder_ = new QLabel(
        "Select a chat from the list\nor click \"+ New chat\" to start a conversation", this);
    timelinePlaceholder_->setAlignment(Qt::AlignCenter);
    timelinePlaceholder_->setStyleSheet(
        "color:#969696; font-size:14pt; background:#141414;");
    rightLayout->addWidget(timelinePlaceholder_);
    timelinePlaceholder_->show();
    timeline_->hide();

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
    connect(messageEdit_, &MessageEdit::sendMessage, this, &MainWindow::onSendMessage);
    connect(messageEdit_, &MessageEdit::slashCommand, this, &MainWindow::onSlashCommand);
}

MainWindow::~MainWindow() {
    sync_.stop();
}

void MainWindow::wireSyncCallbacks() {
    sync_.onSync([this](const FastSyncResponse& resp) {
        QMetaObject::invokeMethod(this, [this, resp]() {
            onSync(resp);
        }, Qt::QueuedConnection);
    });
    sync_.onStateChange([this](SyncEngineState st, const SyncEngineStats& stats) {
        QMetaObject::invokeMethod(this, [this, st, stats]() {
            onSyncState(st, stats);
        }, Qt::QueuedConnection);
    });
    sync_.onAuthError([this]() {
        QMetaObject::invokeMethod(this, [this]() {
            forceReLogin();
        }, Qt::QueuedConnection);
    });
}

void MainWindow::startWithSavedSession() {
    if (!client_ || !client_->isLoggedIn()) return;

    QString userText = QString::fromStdString(client_->account().userId);
    userLabel_->setText(" " + userText + " ");
    statusLabel_->setText("Starting sync...");
    sync_.setClient(client_);
    sync_.setSessionStore(store_);
    sync_.start();
}

void MainWindow::closeEvent(QCloseEvent* e) {
    sync_.stop();
    e->accept();
}

void MainWindow::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_F11) {
        onToggleFullscreen();
        e->accept();
        return;
    }
    QMainWindow::keyPressEvent(e);
}

void MainWindow::onToggleFullscreen() {
    if (!isFullscreen_) {
        showFullScreen();
        isFullscreen_ = true;
        fullscreenAction_->setText("Exit fullscreen");
    } else {
        showNormal();
        isFullscreen_ = false;
        fullscreenAction_->setText("Fullscreen");
    }
}

void MainWindow::onRoomClicked(const QModelIndex& idx) {
    const RoomData* r = roomModel_->at(idx.row());
    if (!r) return;
    currentRoomId_ = QString::fromStdString(r->roomId);
    timeline_->setRoomId(r->roomId);
    timelinePlaceholder_->hide();
    timeline_->show();
    messageEdit_->show();
    messageEdit_->setFocus();
    setWindowTitle(QString("Progressive Chat — %1").arg(
        QString::fromStdString(r->name)));
}

void MainWindow::onSendMessage(const std::string& body) {
    if (currentRoomId_.isEmpty() || !client_) return;
    timeline_->appendLocalEcho(body);

    std::string roomId = currentRoomId_.toStdString();
    MatrixClient* client = client_;
    std::thread([client, roomId, body]() {
        auto r = client->sendMessage(roomId, body);
        if (!r.ok) std::cerr << "send failed: " << r.error.message << "\n";
    }).detach();
}

void MainWindow::onSlashCommand(const std::string& cmd, const std::string& args) {
    if (cmd == "help") {
        timeline_->appendHtml(
            "<p style='color:#969696'>Commands: /help /clear /logout /me &lt;action&gt;</p>");
    } else if (cmd == "clear") {
        timeline_->clear();
    } else if (cmd == "logout") {
        onLogoutClicked();
    } else if (cmd == "me") {
        if (currentRoomId_.isEmpty() || !client_) return;
        std::string roomId = currentRoomId_.toStdString();
        timeline_->appendLocalEcho("* you " + args);
        MatrixClient* client = client_;
        std::thread([client, roomId, body = args]() {
            client->sendMessage(roomId, body, "m.emote");
        }).detach();
    } else {
        timeline_->appendHtml(QString("<p style='color:#969696'>unknown: /%1</p>")
            .arg(QString::fromStdString(cmd)));
    }
}

void MainWindow::onLogoutClicked() {
    sync_.stop();
    if (client_) client_->logout();
    if (store_) store_->clearAccount();
    roomModel_->clear();
    timeline_->clear();
    timeline_->hide();
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
    QString userText = QString::fromStdString(client_->account().userId);
    userLabel_->setText(" " + userText + " ");
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
    timeline_->clear();
    timeline_->hide();
    timelinePlaceholder_->show();
    messageEdit_->hide();
    currentRoomId_.clear();
    userLabel_->setText(" Not logged in ");
    statusLabel_->setText("Session expired. Please login again.");
    showLoginDialog();
}

void MainWindow::onNewChatClicked() {
    if (!client_ || !client_->isLoggedIn()) return;

    // Ask for the user ID to DM
    bool ok = false;
    QString userId = QInputDialog::getText(
        this, "New direct chat",
        "Enter Matrix user ID (e.g. @bob:matrix.org):",
        QLineEdit::Normal, "@", &ok);
    if (!ok || userId.trimmed().isEmpty()) return;
    userId = userId.trimmed();
    if (!userId.startsWith("@")) userId = "@" + userId;

    statusLabel_->setText("Creating direct chat with " + userId + "...");
    QApplication::processEvents();

    std::string uid = userId.toStdString();
    MatrixClient* client = client_;

    // Run in background
    std::thread([this, client, uid]() {
        auto r = client->startDirectMessage(uid);
        // Marshal back to UI thread
        QMetaObject::invokeMethod(this, [this, r]() {
            if (r.ok) {
                statusLabel_->setText("Created room: " + QString::fromStdString(r.data));
            } else {
                QMessageBox::warning(this, "Error",
                    QString("Failed to create chat: %1").arg(QString::fromStdString(r.error.message)));
                statusLabel_->setText("Failed to create chat.");
            }
        }, Qt::QueuedConnection);
    }).detach();
}

void MainWindow::onJoinRoomClicked() {
    if (!client_ || !client_->isLoggedIn()) return;

    bool ok = false;
    QString roomIdOrAlias = QInputDialog::getText(
        this, "Join room",
        "Enter room ID or alias (e.g. #matrix:matrix.org):",
        QLineEdit::Normal, "", &ok);
    if (!ok || roomIdOrAlias.trimmed().isEmpty()) return;

    // TODO: implement join room in MatrixClient
    QMessageBox::information(this, "Coming soon",
        "Join room will be implemented in the next update.");
}

void MainWindow::onSettingsClicked() {
    QMessageBox::information(this, "Settings",
        "Progressive Chat — Desktop\n\n"
        "Version: 0.0.3\n"
        "Phase: 3a.1\n\n"
        "Settings dialog will be added in a future update.\n"
        "For now, use /help in the chat for available commands.");
}

void MainWindow::onSync(const FastSyncResponse& resp) {
    rebuildRoomList(resp);
    int joined = static_cast<int>(resp.joinedRooms.size());
    statusLabel_->setText(QString("synced: %1 chat(s), %2 event(s)")
        .arg(joined).arg(sync_.stats().timelineEvents));
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
    statusLabel_->setText(QString("%1 | %2 chats / %3 events")
        .arg(s).arg(stats.roomsJoined).arg(stats.timelineEvents));
}

void MainWindow::updateRoomListHeader() {
    int n = roomModel_->rowCount();
    roomListHeader_->setText(QString(" Chats (%1) ").arg(n));
}

void MainWindow::rebuildRoomList(const FastSyncResponse& resp) {
    for (const auto& [roomIdView, room] : resp.joinedRooms) {
        std::string roomId(roomIdView);
        RoomData rd;
        rd.roomId = roomId;

        // Compute room name from state events (m.room.name → alias → members)
        rd.name = computeRoomName(room.stateEvents, roomId);

        rd.lastMessage = std::string(extractLastMessageBody(room.timeline.events));
        rd.lastActivityTs = room.timeline.events.empty() ? 0 :
                              room.timeline.events.back().originServerTs;
        if (rd.lastActivityTs == 0 && !room.timeline.events.empty()) {
            rd.lastActivityTs = room.timeline.events.front().originServerTs;
        }

        rd.unreadCount = room.notificationCount;
        rd.highlightCount = room.highlightCount;
        rd.isEncrypted = room.isEncrypted;

        roomModel_->upsertRoom(rd);

        if (currentRoomId_.toStdString() == roomId) {
            appendTimelineForRoom(roomId, room.timeline.events);
        }
    }
    updateRoomListHeader();
}

void MainWindow::appendTimelineForRoom(const std::string& roomId,
                                       const std::vector<FastEvent>& events) {
    for (const auto& e : events) {
        timeline_->appendEvent(std::string(e.eventId), e.originServerTs,
                                std::string(e.senderId), std::string(e.type),
                                std::string(e.contentJson));
    }
}

} // namespace progressive::desktop
