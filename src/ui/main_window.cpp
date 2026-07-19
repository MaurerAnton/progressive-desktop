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
            // Look for "body":"..." in the contentJson
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

    splitter_ = new QSplitter(Qt::Horizontal, this);

    roomList_ = new QListView(splitter_);
    roomList_->setModel(roomModel_ = new RoomListModel(roomList_));
    roomList_->setMinimumWidth(280);
    roomList_->setMaximumWidth(380);
    roomList_->setAlternatingRowColors(true);
    roomList_->setWordWrap(true);
    roomList_->setUniformItemSizes(false);

    auto* rightPanel = new QWidget(splitter_);
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);

    timeline_ = new TimelineView(rightPanel);
    rightLayout->addWidget(timeline_);

    messageEdit_ = new MessageEdit(rightPanel);
    rightLayout->addWidget(messageEdit_);

    splitter_->addWidget(roomList_);
    splitter_->addWidget(rightPanel);
    splitter_->setStretchFactor(0, 1);
    splitter_->setStretchFactor(1, 4);

    setCentralWidget(splitter_);
    statusLabel_ = new QLabel("Not synced yet.", this);
    statusBar()->addWidget(statusLabel_, 1);

    // Sync engine — owns thread, calls back via callbacks (set up in
    // wireSyncCallbacks). SyncEngine is a member of MainWindow.
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
    sync_.onSync([this](const progressive::SyncResponse& resp) {
        QMetaObject::invokeMethod(this, [this, resp]() {
            onSync(resp);
        }, Qt::QueuedConnection);
    });
    sync_.onStateChange([this](SyncEngineState st, const SyncEngineStats& stats) {
        QMetaObject::invokeMethod(this, [this, st, stats]() {
            onSyncState(st, stats);
        }, Qt::QueuedConnection);
    });
}

void MainWindow::startWithSavedSession() {
    if (!client_ || !store_) return;
    if (!client_->isLoggedIn()) return;

    statusLabel_->setText(QString("Logged in as %1")
        .arg(QString::fromStdString(client_->account().userId)));
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
    setWindowTitle(QString("Progressive Chat — %1").arg(
        QString::fromStdString(r->name)));
}

void MainWindow::onSendMessage(const std::string& body) {
    if (currentRoomId_.isEmpty() || !client_) return;
    timeline_->appendLocalEcho(body);

    // Send on a worker thread so we don't block UI
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
        timeline_->appendHtml("<p style='color:#888'>Commands: /help /clear /logout /me &lt;action&gt;</p>");
    } else if (cmd == "clear") {
        timeline_->clear();
    } else if (cmd == "logout") {
        sync_.stop();
        if (client_) client_->logout();
        if (store_) store_->clearAccount();
        roomModel_->clear();
        timeline_->clear();
        currentRoomId_.clear();
        setWindowTitle("Progressive Chat — Desktop");
        statusLabel_->setText("Logged out.");
    } else if (cmd == "me") {
        // /me waves  →  m.emote message
        if (currentRoomId_.isEmpty() || !client_) return;
        std::string roomId = currentRoomId_.toStdString();
        std::string body = args;
        timeline_->appendLocalEcho("* you " + body);
        MatrixClient* client = client_;
        std::thread([client, roomId, body]() {
            client->sendMessage(roomId, body, "m.emote");
        }).detach();
    } else {
        timeline_->appendHtml(QString("<p style='color:#888'>unknown command: /%1</p>")
            .arg(QString::fromStdString(cmd)));
    }
}

void MainWindow::onSync(const FastSyncResponse& resp) {
    rebuildRoomList(resp);
    statusLabel_->setText(QString("synced %1 room(s), %2 event(s)")
        .arg(static_cast<int>(resp.joinedRooms.size()))
        .arg(sync_.stats().timelineEvents));
}

void MainWindow::onSyncState(SyncEngineState state, const SyncEngineStats& stats) {
    const char* s = "?";
    switch (state) {
        case SyncEngineState::Stopped:     s = "stopped"; break;
        case SyncEngineState::InitialSync: s = "initial sync"; break;
        case SyncEngineState::Running:    s = "running"; break;
        case SyncEngineState::Backoff:     s = "backoff"; break;
        case SyncEngineState::Paused:      s = "paused"; break;
    }
    statusLabel_->setText(QString("sync: %1 | total: %2 rooms / %3 events / %4 errors")
        .arg(s).arg(stats.roomsJoined).arg(stats.timelineEvents).arg(stats.errors));
}

void MainWindow::rebuildRoomList(const FastSyncResponse& resp) {
    for (const auto& [roomIdView, room] : resp.joinedRooms) {
        std::string roomId(roomIdView);
        RoomData rd;
        rd.roomId = roomId;

        // Try state events for the name
        std::string name(room.name());
        if (name.empty()) name = roomId;  // last resort
        rd.name = name;

        rd.lastMessage = std::string(extractLastMessageBody(room.timeline.events));
        rd.lastActivityTs = room.timeline.events.empty() ? 0 :
                              room.timeline.events.back().originServerTs;
        if (rd.lastActivityTs == 0 && !room.timeline.events.empty()) {
            // Use the first event if the last is somehow 0
            rd.lastActivityTs = room.timeline.events.front().originServerTs;
        }

        rd.unreadCount = room.notificationCount;
        rd.highlightCount = room.highlightCount;
        rd.isEncrypted = room.isEncrypted;

        roomModel_->upsertRoom(rd);

        // If this is the currently-selected room, append new timeline events
        if (currentRoomId_.toStdString() == roomId) {
            appendTimelineForRoom(roomId, room.timeline.events);
        }
    }
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
