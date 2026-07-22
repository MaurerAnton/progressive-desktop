// src/ui/main_window.cpp — Phase 3 full UI.
#include "main_window.hpp"
#include "toolbar_handler.hpp"
#include "room_handler.hpp"
#include "e2ee_init_handler.hpp"
#include "ui_layout_builder.hpp"
#include "dialogs/login_dialog.hpp"
#include "dialogs/image_viewer_dialog.hpp"
#include "dialogs/threads_dialog.hpp"
#include "dialogs/prefs_dialog.hpp"
#include "chat/emoji_picker.hpp"

#include <QCloseEvent>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListView>
#include <QMessageBox>
#include <QPointer>
#include <QSplitter>
#include <QToolBar>
#include <QAction>
#include <QUrl>
#include <QVBoxLayout>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QTextCursor>
#include <QUuid>

#include <progressive/event_models.hpp>

#include "core/version.h"
#include "core/memory_stats.hpp"

#include <iostream>
#include <thread>
#include <cstdio>

namespace progressive::desktop {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("Progressive Chat — Desktop");
    resize(1100, 720);

    imageLoader_ = new ImageLoader(nullptr, this);
    roomStore_ = new RoomStore(client_, store_);
    roomModel_ = new RoomListModel(this);
    timelineModel_ = new TimelineModel(this);
    timelineDelegate_ = new TimelineDelegate(imageLoader_, this);

    UILayout ui = buildMainWindowLayout(this, imageLoader_, roomModel_,
        timelineModel_, timelineDelegate_);
    toolbar_ = ui.toolbar;
    userLabel_ = ui.userLabel;
    statusLabel_ = ui.statusLabel;
    inviteHeader_ = ui.inviteHeader;
    roomList_ = ui.roomList;
    roomListDelegate_ = ui.roomListDelegate;
    timelineView_ = ui.timelineView;
    timelinePlaceholder_ = ui.timelinePlaceholder;
    loadMoreBtn_ = ui.loadMoreBtn;
    chatLogBtn_ = ui.chatLogBtn;
    threadBtn_ = ui.threadBtn;
    threadBanner_ = ui.threadBanner;
    messageEdit_ = ui.messageEdit;
    splitter_ = ui.splitter;
    roomListHeader_ = ui.roomListHeader;

    toolbarHandler_ = new ToolbarHandler(client_, roomModel_, roomStore_,
        nullptr, statusLabel_, this);
    toolbar_->addAction(toolbarHandler_->createNewChatAction());
    toolbar_->addAction(toolbarHandler_->createJoinRoomAction());
    toolbar_->addAction(toolbarHandler_->createBrowseRoomsAction());
    toolbar_->addAction(toolbarHandler_->createAllThreadsAction());
    toolbar_->addAction(toolbarHandler_->createRoomSettingsAction());
    toolbar_->addAction(toolbarHandler_->createRoomMembersAction());
    toolbar_->addAction(toolbarHandler_->createSettingsAction());
    toolbar_->addSeparator();
    toolbar_->addAction(toolbarHandler_->createFullscreenAction());

    auto* spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar_->addWidget(spacer);

    accountCombo_ = new QComboBox(this);
    accountCombo_->setMinimumWidth(140);
    accountCombo_->setStyleSheet("QComboBox{background:#1a1a1a;color:#ccc;border:1px solid #333;padding:2px 4px;} QComboBox::drop-down{border:none;} QComboBox QAbstractItemView{background:#1a1a1a;color:#ccc;}");
    toolbar_->addWidget(accountCombo_);

    logoutAction_ = toolbar_->addAction("Logout");

    connect(toolbarHandler_, &ToolbarHandler::fullscreenToggled,
            this, &MainWindow::onToggleFullscreen);
    connect(accountCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onSwitchAccount);

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

    connect(threadBanner_, &QLabel::linkActivated, this, [this](const QString& link) {
        if (link == "back" && roomHandler_) roomHandler_->closeThreadView();
    });

    connect(loadMoreBtn_, &QPushButton::clicked, roomHandler_, &RoomHandler::onLoadMoreClicked);
    connect(chatLogBtn_, &QPushButton::clicked, this, &MainWindow::onToggleChatLog);
    connect(threadBtn_, &QPushButton::clicked, this, &MainWindow::toggleThreadPanel);

    wireSyncCallbacks();

    connect(roomList_, &QListView::clicked, roomHandler_, &RoomHandler::onRoomClicked);
    connect(roomList_, &QListView::customContextMenuRequested, roomHandler_, &RoomHandler::onRoomListContextMenu);
    roomList_->setContextMenuPolicy(Qt::CustomContextMenu);

    chatView_ = new ChatView(client_, timelineModel_, messageEdit_, &sync_, this);
    auth_ = new AuthHandler(client_, store_, &sync_, userLabel_, statusLabel_, this);
    roomHandler_ = new RoomHandler(client_, roomStore_, roomModel_, timelineModel_,
        &sync_, imageLoader_, roomList_, timelineView_, statusLabel_, timelinePlaceholder_,
        loadMoreBtn_, chatLogBtn_, messageEdit_, QPointer<MainWindow>(this), this);
    connect(auth_, &AuthHandler::loggedOut, this, [this]() {
        roomModel_->clear(); timelineModel_->clear();
        timelineView_->hide(); timelinePlaceholder_->show();
        messageEdit_->hide(); roomHandler_->clearCurrentRoom();
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

    connect(timelineDelegate_, &TimelineDelegate::imageClicked, this, &MainWindow::onImageClicked);
    connect(timelineDelegate_, &TimelineDelegate::messageClicked, this, &MainWindow::onMessageClicked);

    connect(timelineView_, &QListView::customContextMenuRequested, roomHandler_, &RoomHandler::onTimelineContextMenu);

    connect(logoutAction_, &QAction::triggered, auth_, &AuthHandler::logout);
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

    auto acct = client_->account();
    if (acct.deviceId.empty() || acct.deviceId == "PROGRESSIVE_DESKTOP") {
        acct.deviceId = "pd-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
        client_->setAccount(acct);
        client_->persistSession();
        std::fprintf(stderr, "[session] generated device_id: %s\n", acct.deviceId.c_str());
    }

    std::fprintf(stderr, "[session] loaded: user=%s device=%s homeserver=%s token_prefix=%s refresh=%s\n",
                 acct.userId.c_str(),
                 acct.deviceId.c_str(),
                 acct.homeserverUrl.c_str(),
                 acct.accessToken.substr(0, 8).c_str(),
                 acct.refreshToken.empty() ? "(none)" : (acct.refreshToken.substr(0, 8) + "...").c_str());

    imageLoader_->setClient(client_);
    timelineDelegate_->setMyUserId(client_->account().userId);
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

    E2eeInitHandler::init(client_, store_, &sync_,
        [this](bool ok, bool keysPublished) {
            if (!ok) {
                std::cerr << "[e2ee] init failed — continuing without E2EE\n";
            }
            if (keysPublished) {
                statusLabel_->setText("E2EE ready. Starting sync...");
            } else {
                statusLabel_->setText("E2EE keys uploading...");
            }
            notifier_.init();
            sync_.setClient(client_);
            sync_.setSessionStore(store_);
            logMemorySnapshot("before-first-sync");
            sync_.start();
        });
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
    if (!isFullscreen_) {
        showFullScreen();
        isFullscreen_ = true;
        toolbarHandler_->fullscreenAction()->setText("Exit fullscreen");
    } else {
        showNormal();
        isFullscreen_ = false;
        toolbarHandler_->fullscreenAction()->setText("Fullscreen");
    }
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
        auth_->logout();
    }
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
    statusLabel_->setStyleSheet("");
}

void MainWindow::onSwitchAccount(int index) {
    if (index < 0 || !client_ || !store_) return;
    auto accounts = store_->listAccounts();
    if (index >= (int)accounts.size()) return;

    auto& acct = accounts[index];
    if (acct.userId == client_->account().userId) return;

    accountCombo_->setEnabled(false);

    sync_.stop();

    std::string oldKey = client_->account().userId + "/" + client_->account().deviceId;
    if (sync_.decryptor() && sync_.decryptor()->isInitialized()) {
        auto mp = sync_.decryptor()->megolm()->pickleAll(oldKey);
        if (!mp.empty()) store_->saveMegolmSessions(mp);
        auto op = sync_.decryptor()->pickleOlmSessions(oldKey);
        if (!op.empty()) store_->saveOlmSessions(op);
    }

    roomModel_->clear();
    timelineModel_->clear();
    timelineView_->hide();
    timelinePlaceholder_->show();
    messageEdit_->hide();
    if (roomHandler_) roomHandler_->clearCurrentRoom();
    if (roomHandler_) roomHandler_->memberAvatarCache().clear();
    chatView_->clear();

    client_->setAccount(acct);

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

    userLabel_->setText(" " + QString::fromStdString(acct.userId) + " ");
    timelineDelegate_->setMyUserId(acct.userId);
    imageLoader_->setClient(client_);
    accountCombo_->setCurrentIndex(index);
    accountCombo_->setEnabled(true);

    sync_.setClient(client_);
    sync_.setSessionStore(store_);
    sync_.start();
}

void MainWindow::onImageClicked(const QString& eventId, const QString& mxcUrl) {
    if (mxcUrl.isEmpty()) return;

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
                QDesktopServices::openUrl(QUrl::fromLocalFile(tempPath));
                guard->statusLabel_->setText("Opened " + msgtype.mid(2) + ".");
            }, Qt::QueuedConnection);
        }).detach();
        return;
    }

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

void MainWindow::onToggleChatLog() {
    if (!roomHandler_ || roomHandler_->currentRoomId().empty()) return;
    chatLogging_ = !chatLogging_;

    if (chatLogging_) {
        chatLogBtn_->setChecked(true);
        chatLogBtn_->setText(" Saving");
        QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/chatlogs";
        QDir().mkpath(dir);
        QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd_HHmm");
        std::string roomName = roomHandler_->currentRoomId();
        int row = roomModel_->findRowByRoomId(roomName);
        if (row >= 0) {
            auto* rd = roomModel_->at(row);
            if (rd && !rd->name.empty()) roomName = rd->name;
        }
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
            chatLogBtn_->setText(" Save");
            statusLabel_->setText("Failed to create log file.");
        }
    } else {
        chatLogBtn_->setChecked(false);
        chatLogBtn_->setText(" Save");
        chatLogFile_.reset();
        statusLabel_->setText("Chat log stopped.");
    }
}

void MainWindow::toggleThreadPanel() {
    if (!roomHandler_ || roomHandler_->currentRoomId().empty() || !client_) {
        QMessageBox::information(this, "Threads", "Select a room first.");
        return;
    }
    ThreadsDialog dlg(client_, roomHandler_->currentRoomId(), this);
    dlg.exec();
}

void MainWindow::onSync(FastSyncResponse resp) {
    bool hasData = !resp.joinedRooms.empty() || !resp.leftRoomIds.empty()
                   || !resp.invitedRooms.empty();

    if (hasData && roomStore_) {
        statusLabel_->setText("Syncing...");
        QPointer<MainWindow> guard(this);
        std::string myUserId = client_ ? client_->account().userId : "";
        std::string curRoomId = roomHandler_ ? roomHandler_->currentRoomId() : "";
        QPointer<RoomHandler> rmh(roomHandler_);

        std::thread([guard, rmh, resp = std::move(resp), myUserId, curRoomId]() mutable {
            auto syncUpdate = RoomStore::prepareRoomSyncUpdate(resp, curRoomId, myUserId);

            QMetaObject::invokeMethod(guard, [guard, rmh, syncUpdate = std::move(syncUpdate)]() mutable {
                if (guard.isNull()) return;
                guard->roomStore_->applyRoomSyncUpdate(syncUpdate,
                    guard->roomModel_, guard->timelineModel_);

                for (const auto& rid : syncUpdate.roomsToRemove) {
                    if (!rmh.isNull() && rid == rmh->currentRoomId()) {
                        guard->timelineModel_->clear();
                        rmh->clearCurrentRoom();
                        guard->timelineView_->hide();
                        guard->timelinePlaceholder_->show();
                        guard->messageEdit_->hide();
                        if (guard->loadMoreBtn_) guard->loadMoreBtn_->hide();
                        break;
                    }
                }

                if (syncUpdate.inviteCount > 0) {
                    guard->inviteHeader_->setText(syncUpdate.inviteText);
                    guard->inviteHeader_->show();
                } else {
                    guard->inviteHeader_->hide();
                }
                guard->updateRoomListHeader();
                logMemorySnapshot("after-rebuildRoomList");

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

                guard->statusLabel_->setText(QString("Synced: %1 rooms | %2 messages")
                    .arg(guard->roomModel_->rowCount()).arg(guard->timelineModel_->rowCount()));

                logMemorySnapshot("after-sync-cleanup");
                trimMemory();
            }, Qt::QueuedConnection);
        }).detach();
    }

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
        inviteHeader_->setText(QString("  Invitations (%1) ").arg(inviteCount));
        inviteHeader_->show();
    } else {
        inviteHeader_->hide();
    }
}

} // namespace progressive::desktop
