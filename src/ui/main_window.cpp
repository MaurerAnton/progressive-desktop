// src/ui/main_window.cpp

#include "main_window.hpp"
#include "login_dialog.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QListView>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QAction>
#include <QVBoxLayout>
#include <QStandardPaths>
#include <QDir>

#include <progressive/markdown.hpp>
#include <progressive/json_parser.hpp>
#include <progressive/event_models.hpp>
#include <progressive/room_name.hpp>

#include <iostream>
#include <chrono>

namespace progressive::desktop {

namespace {

// Extract last text message body from timeline events (newest first).
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

    // --- Toolbar (top) ---
    toolbar_ = addToolBar("Main");
    toolbar_->setMovable(false);
    userLabel_ = new QLabel("Not logged in", this);
    toolbar_->addWidget(userLabel_);
    toolbar_->addSeparator();
    logoutAction_ = toolbar_->addAction("Logout");
    connect(logoutAction_, &QAction::triggered, this, &MainWindow::onLogoutClicked);

    // --- Central splitter ---
    splitter_ = new QSplitter(Qt::Horizontal, this);

    // Left panel: room list header + room list
    auto* leftPanel = new QWidget(splitter_);
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);

    roomListHeader_ = new QLabel("Rooms (0)", this);
    roomListHeader_->setStyleSheet(
        "font-weight:600; padding:8px 12px; color:#e8e8e8; background:#1e1e1e;");
    leftLayout->addWidget(roomListHeader_);

    roomList_ = new QListView(leftPanel);
    roomList_->setModel(roomModel_ = new RoomListModel(roomList_));
    roomList_->setMinimumWidth(280);
    roomList_->setMaximumWidth(380);
    roomList_->setAlternatingRowColors(true);
    roomList_->setWordWrap(true);
    roomList_->setUniformItemSizes(false);
    leftLayout->addWidget(roomList_);

    // Right panel: timeline + message edit
    auto* rightPanel = new QWidget(splitter_);
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);

    timeline_ = new TimelineView(rightPanel);
    rightLayout->addWidget(timeline_);

    // Placeholder shown when no room is selected
    timelinePlaceholder_ = new QLabel(
        "← Select a room from the list to start chatting", this);
    timelinePlaceholder_->setAlignment(Qt::AlignCenter);
    timelinePlaceholder_->setStyleSheet(
        "color:#969696; font-size:14pt; background:#141414;");
    rightLayout->addWidget(timelinePlaceholder_);
    timelinePlaceholder_->show();
    timeline_->hide();

    messageEdit_ = new MessageEdit(rightPanel);
    messageEdit_->hide();  // hidden until a room is selected
    rightLayout->addWidget(messageEdit_);

    splitter_->addWidget(leftPanel);
    splitter_->addWidget(rightPanel);
    splitter_->setStretchFactor(0, 1);
    splitter_->setStretchFactor(1, 4);

    setCentralWidget(splitter_);
    statusLabel_ = new QLabel("Not synced yet.", this);
    statusBar()->addWidget(statusLabel_, 1);

    // Sync engine — owns thread, calls back via callbacks.
    wireSyncCallbacks();

    // Wire UI signals
    connect(roomList_, &QListView::clicked, this, &MainWindow::onRoomClicked);
    connect(messageEdit_, &MessageEdit::sendMessage, this, &MainWindow::onSendMessage);
    connect(messageEdit_, &MessageEdit::slashCommand, this, &MainWindow::onSlashCommand);

    // Mobile mode — fullscreen frameless
#ifdef PROGRESSIVE_MOBILE
    setWindowFlags(Qt::FramelessWindowHint);
    showFullScreen();
#endif
}

MainWindow::~MainWindow() {
    sync_.stop();
}

void MainWindow::wireSyncCallbacks() {
    // SyncEngine calls these from its worker thread — marshal to UI thread.
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
    // M_UNKNOWN_TOKEN → clear session, show login dialog
    sync_.onAuthError([this]() {
        QMetaObject::invokeMethod(this, [this]() {
            forceReLogin();
        }, Qt::QueuedConnection);
    });
}

void MainWindow::startWithSavedSession() {
    if (!client_ || !store_) return;
    if (!client_->isLoggedIn()) return;

    QString userText = QString::fromStdString(client_->account().userId);
    userLabel_->setText("Logged in: " + userText);
    statusLabel_->setText("Starting sync...");
    sync_.setClient(client_);
    sync_.setSessionStore(store_);
    sync_.start();
}

void MainWindow::closeEvent(QCloseEvent* e) {
    sync_.stop();
    e->accept();
}

void MainWindow::onRoomClicked(const QModelIndex& idx) {
    const RoomData* r = roomModel_->at(idx.row());
    if (!r) return;
    currentRoomId_ = QString::fromStdString(r->roomId);
    timeline_->setRoomId(r->roomId);
    timelinePlaceholder_->hide();
    timeline_->show();
    messageEdit_->show();
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
        if (!r.ok) {
            std::cerr << "send failed: " << r.error.message << "\n";
        }
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
        std::string body = args;
        timeline_->appendLocalEcho("* you " + body);
        MatrixClient* client = client_;
        std::thread([client, roomId, body]() {
            client->sendMessage(roomId, body, "m.emote");
        }).detach();
    } else {
        timeline_->appendHtml(QString("<p style='color:#969696'>unknown command: /%1</p>")
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
    userLabel_->setText("Not logged in");
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
    if (client_ && client_->isLoggedIn()) {
        QString userText = QString::fromStdString(client_->account().userId);
        userLabel_->setText("Logged in: " + userText);
        statusLabel_->setText("Starting sync...");
        sync_.setClient(client_);
        sync_.setSessionStore(store_);
        sync_.start();
    }
}

void MainWindow::forceReLogin() {
    // Called when sync returns M_UNKNOWN_TOKEN.
    sync_.stop();
    if (store_) store_->clearAccount();
    if (client_) {
        AccountInfo empty;
        client_->setAccount(empty);
    }
    roomModel_->clear();
    timeline_->clear();
    timeline_->hide();
    timelinePlaceholder_->show();
    messageEdit_->hide();
    currentRoomId_.clear();
    userLabel_->setText("Not logged in");
    statusLabel_->setText("Session expired. Please login again.");
    showLoginDialog();
}

void MainWindow::onSync(const FastSyncResponse& resp) {
    rebuildRoomList(resp);
    int joined = static_cast<int>(resp.joinedRooms.size());
    statusLabel_->setText(QString("synced %1 room(s), %2 event(s) | %3 bytes received")
        .arg(joined)
        .arg(sync_.stats().timelineEvents)
        .arg(resp.buffer ? (int)resp.buffer->size() : 0));
}

void MainWindow::onSyncState(SyncEngineState state, const SyncEngineStats& stats) {
    const char* s = "?";
    switch (state) {
        case SyncEngineState::Stopped:     s = "stopped"; break;
        case SyncEngineState::InitialSync: s = "initial sync (downloading rooms)..."; break;
        case SyncEngineState::Running:    s = "synced"; break;
        case SyncEngineState::Backoff:     s = "connection issue, retrying..."; break;
        case SyncEngineState::Paused:      s = "paused"; break;
    }
    statusLabel_->setText(QString("sync: %1 | %2 rooms / %3 events / %4 errors")
        .arg(s).arg(stats.roomsJoined).arg(stats.timelineEvents).arg(stats.errors));
}

void MainWindow::updateRoomListHeader() {
    int n = roomModel_->rowCount();
    roomListHeader_->setText(QString("Rooms (%1)").arg(n));
}

void MainWindow::rebuildRoomList(const FastSyncResponse& resp) {
    for (const auto& [roomIdView, room] : resp.joinedRooms) {
        std::string roomId(roomIdView);
        RoomData rd;
        rd.roomId = roomId;

        std::string name(room.name());
        if (name.empty()) name = roomId;
        rd.name = name;

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
