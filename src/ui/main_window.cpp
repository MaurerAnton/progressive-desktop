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

#include <iostream>
#include <chrono>
#include <thread>

namespace progressive::desktop {

namespace {

std::string_view extractLastMessageBody(const std::vector<FastEvent>& events) {
    for (auto it = events.rbegin(); it != events.rend(); ++it) {
        const auto& e = *it;
        if (e.type == "m.room.message" && !e.contentJson.empty()) {
            auto pos = e.contentJson.find("\"body\":\"");
            if (pos != std::string_view::npos) {
                pos += 7;
                auto end = e.contentJson.find('"', pos);
                if (end != std::string_view::npos) {
                    return e.contentJson.substr(pos, end - pos);
                }
            }
        }
    }
    return {};
}

std::string computeRoomName(const std::vector<FastEvent>& stateEvents, const std::string& roomId) {
    // m.room.name
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
    // m.room.canonical_alias
    for (const auto& e : stateEvents) {
        if (e.type == "m.room.canonical_alias" && !e.contentJson.empty()) {
            auto pos = e.contentJson.find("\"alias\":\"");
            if (pos != std::string::npos) {
                pos += 8;
                auto start = e.contentJson.find('"', pos) + 1;
                auto end = e.contentJson.find('"', start);
                if (end != std::string::npos) return std::string(e.contentJson.substr(start, end - start));
            }
        }
    }
    // Members
    std::vector<std::string> members;
    for (const auto& e : stateEvents) {
        if (e.type == "m.room.member" && !e.contentJson.empty()) {
            auto mpos = e.contentJson.find("\"membership\":\"");
            std::string_view membership;
            if (mpos != std::string::npos) {
                mpos += 13;
                auto mend = e.contentJson.find('"', mpos);
                if (mend != std::string::npos) membership = e.contentJson.substr(mpos, mend - mpos);
            }
            if (membership != "join") continue;
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
            if (!e.senderId.empty()) {
                std::string_view uid = e.senderId;
                if (uid[0] == '@') {
                    auto colon = uid.find(':');
                    if (colon != std::string::npos) members.push_back(std::string(uid.substr(1, colon - 1)));
                }
            }
        }
    }
    std::sort(members.begin(), members.end());
    members.erase(std::unique(members.begin(), members.end()), members.end());
    if (members.size() == 1) return members[0];
    if (members.size() == 2) return members[0] + ", " + members[1];
    if (members.size() > 2) return members[0] + " + " + std::to_string(members.size() - 1);
    return "Без названия";
}

// Extract mxc URL from image content JSON
std::string extractMxcUrl(std::string_view contentJson) {
    auto pos = contentJson.find("\"url\":\"");
    if (pos == std::string_view::npos) return {};
    pos += 7;
    auto start = pos;
    while (start < contentJson.size() && contentJson[start] != '"') {
        if (contentJson[start] == '\\') start++;
        start++;
    }
    if (start > pos) return std::string(contentJson.substr(pos, start - pos));
    return {};
}

// Extract mimetype from content JSON
std::string extractMimetype(std::string_view contentJson) {
    auto pos = contentJson.find("\"mimetype\":\"");
    if (pos == std::string_view::npos) return {};
    pos += 12;
    auto end = contentJson.find('"', pos);
    if (end != std::string_view::npos) return std::string(contentJson.substr(pos, end - pos));
    return {};
}

// Extract body from content JSON
std::string extractBody(std::string_view contentJson) {
    auto pos = contentJson.find("\"body\":\"");
    if (pos == std::string_view::npos) return {};
    pos += 7;
    auto start = contentJson.find('"', pos) + 1;
    auto end = contentJson.find('"', start);
    if (end != std::string_view::npos) return std::string(contentJson.substr(start, end - start));
    return {};
}

// Extract msgtype from content JSON
std::string extractMsgtype(std::string_view contentJson) {
    auto pos = contentJson.find("\"msgtype\":\"");
    if (pos == std::string_view::npos) return {};
    pos += 11;
    auto end = contentJson.find('"', pos);
    if (end != std::string_view::npos) return std::string(contentJson.substr(pos, end - pos));
    return {};
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
    std::thread([this, client, uid]() {
        auto r = client->startDirectMessage(uid);
        QMetaObject::invokeMethod(this, [this, r]() {
            if (r.ok) statusLabel_->setText("Created room: " + QString::fromStdString(r.data));
            else { QMessageBox::warning(this, "Error", QString("Failed: %1").arg(QString::fromStdString(r.error.message)));
                   statusLabel_->setText("Failed."); }
        }, Qt::QueuedConnection);
    }).detach();
}

void MainWindow::onJoinRoomClicked() {
    if (!client_ || !client_->isLoggedIn()) return;
    bool ok;
    QString roomIdOrAlias = QInputDialog::getText(this, "Join room",
        "Enter room ID or alias (e.g. #matrix:matrix.org):", QLineEdit::Normal, "", &ok);
    if (!ok || roomIdOrAlias.trimmed().isEmpty()) return;
    statusLabel_->setText("Joining...");
    std::string id = roomIdOrAlias.trimmed().toStdString();
    MatrixClient* client = client_;
    std::thread([this, client, id]() {
        auto r = client->joinRoom(id);
        QMetaObject::invokeMethod(this, [this, r]() {
            if (r.ok) { statusLabel_->setText("Joined: " + QString::fromStdString(r.data));
                        QMessageBox::information(this, "Joined", "Successfully joined room."); }
            else { QMessageBox::warning(this, "Error", QString("Failed: %1").arg(QString::fromStdString(r.error.message)));
                   statusLabel_->setText("Failed."); }
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
    if (currentRoomId_.isEmpty() || !client_) {
        QMessageBox::information(this, "Threads", "Select a room first.");
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
    // Fetch full image, then show viewer
    if (mxcUrl.isEmpty()) return;
    std::string mxc = mxcUrl.toStdString();
    MatrixClient* client = client_;

    statusLabel_->setText("Loading full image...");
    std::thread([this, client, mxc]() {
        auto r = client->downloadMedia(mxc, 0, 0);
        QImage img;
        if (r.ok && !r.data.empty()) {
            img.loadFromData(r.data.data(), static_cast<int>(r.data.size()));
        }
        QMetaObject::invokeMethod(this, [this, img, mxc]() {
            if (img.isNull()) {
                QMessageBox::warning(this, "Error", "Failed to load image.");
                statusLabel_->setText("Failed to load image.");
                return;
            }
            auto* dlg = new ImageViewerDialog(img, QString::fromStdString(mxc), this);
            dlg->exec();
            delete dlg;
            statusLabel_->setText("Ready.");
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
        std::thread([this, client, roomId, eid]() {
            auto r = client->pinMessage(roomId, eid);
            QMetaObject::invokeMethod(this, [this, r, eid]() {
                if (r.ok) { timelineModel_->setPinned(eid, true);
                            statusLabel_->setText("Message pinned."); }
                else statusLabel_->setText("Pin failed: " + QString::fromStdString(r.error.message));
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
        std::thread([this, client, roomId, eid]() {
            auto r = client->redactEvent(roomId, eid);
            QMetaObject::invokeMethod(this, [this, r]() {
                if (r.ok) statusLabel_->setText("Message redacted.");
                else statusLabel_->setText("Redact failed: " + QString::fromStdString(r.error.message));
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
    for (const auto& [roomIdView, room] : resp.joinedRooms) {
        std::string roomId(roomIdView);
        RoomData rd;
        rd.roomId = roomId;
        rd.name = computeRoomName(room.stateEvents, roomId);
        rd.lastMessage = std::string(extractLastMessageBody(room.timeline.events));
        rd.lastActivityTs = room.timeline.events.empty() ? 0 : room.timeline.events.back().originServerTs;
        if (rd.lastActivityTs == 0 && !room.timeline.events.empty())
            rd.lastActivityTs = room.timeline.events.front().originServerTs;
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

void MainWindow::appendTimelineForRoom(const std::string& roomId, const std::vector<FastEvent>& events) {
    for (const auto& e : events) {
        timelineModel_->appendBack(fastEventToDisplayed(e));
    }
}

} // namespace progressive::desktop
