// src/ui/main_window.cpp — Phase 3 full UI.
#include "main_window.hpp"
#include "shared/theme.hpp"
#include "handlers/toolbar_handler.hpp"
#include "handlers/room_handler.hpp"
#include "handlers/thread_handler.hpp"
#include "handlers/verification_handler.hpp"
#include "ui_layout_builder.hpp"
#include "dialogs/login_dialog.hpp"
#include "dialogs/image_viewer_dialog.hpp"
#include "dialogs/threads_dialog.hpp"
#include "dialogs/prefs_dialog.hpp"
#include "dialogs/room_switcher_dialog.hpp"
#include "chat/emoji_picker.hpp"

#include <QCloseEvent>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListView>
#include <QMessageBox>
#include <QPointer>
#include <QApplication>
#include <QStatusBar>
#include <cstdlib>
#include <QSplitter>
#include <QToolBar>
#include <QAction>
#include <QUrl>
#include <QVBoxLayout>
#include <QFile>
#include <QTextCursor>

#include "core/utils.hpp"

#include <progressive/event_models.hpp>

#include "core/version.h"
#include "core/memory_stats.hpp"
#include "core/debug_log.hpp"
#include "core/thread_pool.hpp"

#include "handlers/sync_response_handler.hpp"
#include "handlers/attachment_handler.hpp"
#include "handlers/session_bootstrap.hpp"
#include "handlers/slash_command_handler.hpp"
#include "handlers/account_switcher.hpp"

#include <chrono>
#include <iostream>
#include <cstdio>

namespace progressive::desktop {

namespace {
inline constexpr int kDefaultWinW = 1100;
inline constexpr int kDefaultWinH = 720;
inline constexpr int kAccountComboW = 140;
}

void MainWindow::setClient(std::shared_ptr<MatrixClient> client) {
    client_ = std::move(client);
    sync_.setClient(client_);
    sync_.setPollTimeout(PrefsDialog::pollTimeoutMs());
    sync_.setBackupPathProvider([]() -> std::string {
        const char* dataHome = std::getenv("XDG_DATA_HOME");
        if (!dataHome || !dataHome[0]) {
            const char* home = std::getenv("HOME");
            if (!home || !home[0]) return {};
            static std::string homeData;
            homeData = std::string(home) + "/.local/share";
            dataHome = homeData.c_str();
        }
        return std::string(dataHome) + "/progressive-desktop/sessions_backup/";
    });
    client_->setInvisibleMode(PrefsDialog::invisibleMode());
    sync_.decryptor()->setShareKeysVerifiedOnly(PrefsDialog::shareKeysVerifiedOnly());
    if (imageLoader_) imageLoader_->setClient(client_);
    if (chatView_) chatView_->setClient(client_);
    if (roomStore_) roomStore_->setClient(client_);
    if (toolbarHandler_) toolbarHandler_->setClient(client_);
    if (roomHandler_) roomHandler_->setClient(client_);
    if (auth_) auth_->setClient(client_);
    if (attachmentHandler_) attachmentHandler_->setClient(client_);
    if (accountSwitcher_) accountSwitcher_->setClient(client_);
    if (syncHandler_) syncHandler_->setClient(client_);
    if (verifyHandler_) verifyHandler_->setClient(client_);
}

void MainWindow::setSessionStore(std::shared_ptr<SessionStore> store) {
    store_ = std::move(store);
    if (roomStore_) roomStore_->setSessionStore(store_);
    if (accountSwitcher_) accountSwitcher_->setSessionStore(store_);
    if (roomModel_ && store_) {
        auto hidden = store_->loadHiddenRooms();
        roomModel_->setHiddenRooms({hidden.begin(), hidden.end()});
    }
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("Progressive Chat — Desktop");
    resize(kDefaultWinW, kDefaultWinH);

    imageLoader_ = new ImageLoader(nullptr, this);
    roomStore_ = new RoomStore(client_, store_);
    roomModel_ = new RoomListModel(this);
    timelineModel_ = new TimelineModel(this);
    timelineDelegate_ = new TimelineDelegate(imageLoader_, this);
    if (timelineModel_) timelineModel_->setImageLoader(imageLoader_);

    UILayout ui = buildMainWindowLayout(this, imageLoader_, roomModel_,
        timelineModel_, timelineDelegate_);
    toolbar_ = ui.toolbar;
    userLabel_ = ui.userLabel;
    statusLabel_ = ui.statusLabel;
    inviteHeader_ = ui.inviteHeader;
    roomList_ = ui.roomList;
    roomListDelegate_ = ui.roomListDelegate;
    timelineView_ = ui.timelineView;
    timelineModel_->setView(timelineView_);
    connect(timelineModel_, &QAbstractItemModel::modelAboutToBeReset, timelineDelegate_, [this]() {
        timelineDelegate_->invalidateLayoutCache();
    });
    connect(timelineModel_, &QAbstractItemModel::dataChanged, timelineDelegate_, [this]() {
        timelineDelegate_->invalidateLayoutCache();
    });
    timelinePlaceholder_ = ui.timelinePlaceholder;
    loadMoreBtn_ = ui.loadMoreBtn;
    chatLogBtn_ = ui.chatLogBtn;
    threadBtn_ = ui.threadBtn;
    threadBanner_ = ui.threadBanner;
    messageEdit_ = ui.messageEdit;
    splitter_ = ui.splitter;
    roomListHeader_ = ui.roomListHeader;

    roomList_->setFocusPolicy(Qt::StrongFocus);
    timelineView_->setFocusPolicy(Qt::StrongFocus);
    messageEdit_->setFocusPolicy(Qt::StrongFocus);
    if (splitter_) splitter_->setFocusPolicy(Qt::ClickFocus);

    roomModel_->setHeaderLabels(roomListHeader_, inviteHeader_);

    toolbarHandler_ = new ToolbarHandler(client_, roomModel_, roomStore_,
        timelineModel_, statusLabel_, &sync_, this);
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
    accountCombo_->setMinimumWidth(kAccountComboW);
    accountCombo_->setStyleSheet("QComboBox{background:" + Design::accountComboBg.name() + ";color:" + Design::accountComboText.name() + ";border:1px solid " + Design::borderColor.name() + ";padding:2px 4px;} QComboBox::drop-down{border:none;} QComboBox QAbstractItemView{background:" + Design::accountComboBg.name() + ";color:" + Design::accountComboText.name() + ";}");
    Theme::addListener([guard = QPointer<QComboBox>(accountCombo_)]() {
        if (!guard) return;
        guard->setStyleSheet("QComboBox{background:" + Design::accountComboBg.name() + ";color:" + Design::accountComboText.name() + ";border:1px solid " + Design::borderColor.name() + ";padding:2px 4px;} QComboBox::drop-down{border:none;} QComboBox QAbstractItemView{background:" + Design::accountComboBg.name() + ";color:" + Design::accountComboText.name() + ";}");
    });
    toolbar_->addWidget(accountCombo_);

    connect(threadBanner_, &QLabel::linkActivated, this, [this](const QString& link) {
        if (link == "back" && roomHandler_) roomHandler_->closeThreadView();
    });

    wireSyncCallbacks();

    chatView_ = new ChatView(client_, timelineModel_, messageEdit_, &sync_, this);
    chatView_->setChatLogger(toolbarHandler_->chatLogger());
    chatView_->setRoomListModel(roomModel_);
    auth_ = new AuthHandler(client_, store_, &sync_, userLabel_, statusLabel_, this);
    roomHandler_ = new RoomHandler(client_, roomStore_, roomModel_, timelineModel_,
        &sync_, imageLoader_, roomList_, timelineView_, statusLabel_, timelinePlaceholder_,
        loadMoreBtn_, chatLogBtn_, messageEdit_, QPointer<MainWindow>(this), this);

    connect(roomListDelegate_, &RoomListDelegate::inviteAccepted, roomHandler_, &RoomHandler::acceptInvite);
    connect(roomListDelegate_, &RoomListDelegate::inviteRejected, roomHandler_, &RoomHandler::rejectInvite);

    toolbarHandler_->setRoomHandler(roomHandler_);
    toolbarHandler_->setAuthHandler(auth_);
    toolbarHandler_->setInterfaceElements(chatLogBtn_, threadBtn_);
    connect(toolbarHandler_, &ToolbarHandler::prefsChanged, this, [this]() {
        sync_.setPollTimeout(PrefsDialog::pollTimeoutMs());
        client_->setInvisibleMode(PrefsDialog::invisibleMode());
        sync_.decryptor()->setShareKeysVerifiedOnly(PrefsDialog::shareKeysVerifiedOnly());
    });
    connect(chatLogBtn_, &QPushButton::clicked, toolbarHandler_, &ToolbarHandler::onToggleChatLog);
    connect(threadBtn_, &QPushButton::clicked, toolbarHandler_, &ToolbarHandler::toggleThreadPanel);

    connect(loadMoreBtn_, &QPushButton::clicked, roomHandler_, &RoomHandler::onLoadMoreClicked);
    connect(roomList_, &QListView::clicked, roomHandler_, &RoomHandler::onRoomClicked);
    connect(roomList_, &QListView::customContextMenuRequested, roomHandler_, &RoomHandler::onRoomListContextMenu);
    roomList_->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(auth_, &AuthHandler::loggedOut, this, [this]() {
        roomModel_->clear(); timelineModel_->clear();
        timelineView_->hide(); timelinePlaceholder_->show();
        messageEdit_->hide(); roomHandler_->clearCurrentRoom();
        setWindowTitle("Progressive Chat — Desktop");
        auth_->showLoginDialog();
    });

    connect(messageEdit_, &MessageEdit::imagePasted, chatView_, &ChatView::onImagePasted);

    connect(messageEdit_, &MessageEdit::emojiPickerRequested, this, [this]() {
        EmojiPicker picker(this);
        connect(&picker, &EmojiPicker::emojiSelected, this, [this](const QString& emoji) {
            QTextCursor c = messageEdit_->textEdit()->textCursor();
            c.insertText(emoji);
            messageEdit_->setFocus();
        });
        picker.exec();
    });

    connect(timelineDelegate_, &TimelineDelegate::messageClicked, this, &MainWindow::onMessageClicked);
    connect(timelineDelegate_, &TimelineDelegate::reactionClicked, this, [this](const QString& eventId, const QString& emojiWithCount) {
        if (roomHandler_->currentRoomId().empty() || !client_) return;
        auto client = client_;
        std::string roomId = roomHandler_->currentRoomId();
        std::string eid = eventId.toStdString();
        std::string em = emojiWithCount.section(' ', 0, 0).toStdString();
        std::string myUserId = client_->account().userId;
        std::string existingId = timelineModel_->myReactionId(eid, em, myUserId);
        if (!existingId.empty()) {
            ThreadPool::instance().enqueue([client, roomId, existingId]() {
                client->redactEvent(roomId, existingId, "toggle");
            });
        } else {
            ThreadPool::instance().enqueue([client, roomId, eid, em]() {
                client->sendReaction(roomId, eid, em);
            });
        }
    });

    connect(timelineDelegate_, &TimelineDelegate::doubleClicked, this, [this](const QString& eventId) {
        if (roomHandler_->currentRoomId().empty() || !client_) return;
        auto client = client_;
        std::string roomId = roomHandler_->currentRoomId();
        std::string eid = eventId.toStdString();
        std::string myUserId = client_->account().userId;
        int row = timelineModel_->findRow(eid);
        if (row >= 0) {
            auto* evt = timelineModel_->at(row);
            if (evt && evt->senderId == myUserId) return;
        }
        std::string emoji = "\xe2\x9d\xa4\xef\xb8\x8f";
        std::string existingId = timelineModel_->myReactionId(eid, emoji, myUserId);
        if (!existingId.empty()) {
            ThreadPool::instance().enqueue([client, roomId, existingId]() {
                client->redactEvent(roomId, existingId, "toggle");
            });
        } else {
            ThreadPool::instance().enqueue([client, roomId, eid, emoji]() {
                client->sendReaction(roomId, eid, emoji);
            });
        }
    });

    connect(timelineView_, &QListView::customContextMenuRequested, roomHandler_, &RoomHandler::onTimelineContextMenu);
    connect(timelineDelegate_, &TimelineDelegate::threadIndicatorClicked, roomHandler_, &RoomHandler::openThreadView);

    syncHandler_ = new SyncResponseHandler(client_, roomStore_, roomModel_,
        timelineModel_, &notifier_, roomListHeader_, inviteHeader_,
        statusLabel_, timelinePlaceholder_, timelineView_,
        messageEdit_, loadMoreBtn_, roomHandler_, this);
    syncHandler_->setDecryptor(sync_.decryptor());
    attachmentHandler_ = new AttachmentHandler(client_, timelineModel_, statusLabel_, this);
    slashHandler_ = new SlashCommandHandler(timelineModel_, auth_, this);
    accountSwitcher_ = new AccountSwitcher(client_, store_, &sync_,
        accountCombo_, userLabel_, statusLabel_, roomModel_, timelineModel_,
        imageLoader_, timelineDelegate_, roomHandler_, chatView_,
        timelinePlaceholder_, timelineView_, messageEdit_, this);

    verifyHandler_ = new VerificationHandler(this);
    verifyHandler_->setSyncEngine(&sync_);
    if (statusBar()) statusBar()->addWidget(verifyHandler_->bannerWidget(), 1);

    toolbarHandler_->setVerificationHandler(verifyHandler_);

    connect(accountCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
                int accountCount = accountSwitcher_->accountCount();
                if (index < accountCount) {
                    accountSwitcher_->switchAccount(index);
                } else if (index == accountCount + 1) {
                    accountCombo_->blockSignals(true);
                    accountCombo_->setCurrentIndex(accountSwitcher_->currentAccountIndex());
                    accountCombo_->blockSignals(false);
                    accountSwitcher_->addAccount();
                } else if (index == accountCount + 2) {
                    accountCombo_->blockSignals(true);
                    accountCombo_->setCurrentIndex(accountSwitcher_->currentAccountIndex());
                    accountCombo_->blockSignals(false);
                    auth_->logout();
                }
            });

    connect(timelineDelegate_, &TimelineDelegate::imageClicked, this, [this](const QString& eventId, const QString& mxcUrl) {
        attachmentHandler_->openAttachment(eventId, mxcUrl);
    });
    connect(chatView_, &ChatView::slashCommandForward, slashHandler_, [this](const std::string& cmd, const std::string& args) {
        slashHandler_->handleCommand(cmd, args);
    });
}

MainWindow::~MainWindow() {
    sync_.stop();
}

void MainWindow::wireSyncCallbacks() {
    sync_.onSync([this](FastSyncResponse resp) {
        QMetaObject::invokeMethod(syncHandler_, [this, resp = std::move(resp)]() mutable {
            syncHandler_->handle(std::move(resp));
        }, Qt::QueuedConnection);
    });
    sync_.onStateChange([this](SyncEngineState st, const SyncEngineStats& stats) {
        QMetaObject::invokeMethod(this, [this, st, stats]() { onSyncState(st, stats); }, Qt::QueuedConnection);
    });
    sync_.onAuthError([this]() {
        QMetaObject::invokeMethod(auth_, &AuthHandler::forceReLogin, Qt::QueuedConnection);
    });
}

void MainWindow::startWithSavedSession() {
    SessionBootstrap::start(client_, store_, &sync_, accountCombo_, userLabel_,
                            statusLabel_, imageLoader_, timelineDelegate_, &notifier_);
}

void MainWindow::closeEvent(QCloseEvent* e) {
    // Stop the sync loop FIRST — the final save must not race the sync
    // thread's decryptor activity (was a same-thread double-lock crash).
    sync_.stop();
    sync_.persistCrypto();
    QMainWindow::closeEvent(e);
    QApplication::quit();
}

void MainWindow::keyPressEvent(QKeyEvent* e) {
    // Room switching
    if (e->key() == Qt::Key_Down && (e->modifiers() & Qt::AltModifier)) {
        int row = roomList_->currentIndex().row() + 1;
        if (row < roomModel_->rowCount())
            roomList_->setCurrentIndex(roomModel_->index(row));
        e->accept(); return;
    }
    if (e->key() == Qt::Key_Up && (e->modifiers() & Qt::AltModifier)) {
        int row = roomList_->currentIndex().row() - 1;
        if (row >= 0)
            roomList_->setCurrentIndex(roomModel_->index(row));
        e->accept(); return;
    }
    // Jump to Nth room
    if (e->modifiers() & Qt::ControlModifier && e->key() >= Qt::Key_1 && e->key() <= Qt::Key_9) {
        int n = e->key() - Qt::Key_1;
        if (n < roomModel_->rowCount())
            roomList_->setCurrentIndex(roomModel_->index(n));
        e->accept(); return;
    }
    // Message navigation
    if (e->key() == Qt::Key_Down && (e->modifiers() & Qt::ControlModifier)) {
        int row = timelineView_->currentIndex().row() + 1;
        if (row < timelineModel_->rowCount())
            timelineView_->setCurrentIndex(timelineModel_->index(row));
        e->accept(); return;
    }
    if (e->key() == Qt::Key_Up && (e->modifiers() & Qt::ControlModifier)) {
        int row = timelineView_->currentIndex().row() - 1;
        if (row >= 0)
            timelineView_->setCurrentIndex(timelineModel_->index(row));
        e->accept(); return;
    }
    // Quick reply + react on selected message
    if (e->key() == Qt::Key_R && (e->modifiers() & Qt::ControlModifier) &&
        !(e->modifiers() & Qt::ShiftModifier)) {
        QModelIndex idx = timelineView_->currentIndex();
        QString eid = idx.data(TimelineModel::EventIdRole).toString();
        if (!eid.isEmpty() && roomHandler_) {
            std::string roomId = roomHandler_->currentRoomId();
            if (!roomId.empty() && roomHandler_->threadHandler())
                roomHandler_->threadHandler()->replyInThread(eid, roomId);
        }
        e->accept(); return;
    }
    if (e->key() == Qt::Key_K && (e->modifiers() & Qt::ControlModifier) &&
        !(e->modifiers() & Qt::ShiftModifier)) {
        RoomSwitcherDialog dlg(roomModel_, this);
        connect(&dlg, &RoomSwitcherDialog::roomSelected, this, [this](const QString& roomId) {
            int row = roomModel_->findRowByRoomId(roomId.toStdString());
            if (row >= 0 && roomHandler_)
                roomHandler_->onRoomClicked(roomModel_->index(row));
        });
        dlg.exec();
        e->accept(); return;
    }
    if (e->key() == Qt::Key_K && (e->modifiers() & Qt::ControlModifier) &&
        (e->modifiers() & Qt::ShiftModifier)) {
        QModelIndex idx = timelineView_->currentIndex();
        QString eid = idx.data(TimelineModel::EventIdRole).toString();
        if (!eid.isEmpty() && roomHandler_ && client_) {
            auto client = client_;
            std::string roomId = roomHandler_->currentRoomId();
            std::string eidStr = eid.toStdString();
            std::string myUserId = client_->account().userId;
            int row = timelineModel_->findRow(eid.toStdString());
            if (row >= 0) {
                auto* evt = timelineModel_->at(row);
                if (evt && evt->senderId == myUserId) { e->accept(); return; }
            }
            std::string emoji = "\xe2\x9d\xa4\xef\xb8\x8f";
            std::string existingId = timelineModel_->myReactionId(eidStr, emoji, myUserId);
            if (!existingId.empty()) {
                ThreadPool::instance().enqueue([client, roomId, existingId]() {
                    client->redactEvent(roomId, existingId, "toggle");
                });
            } else {
                ThreadPool::instance().enqueue([client, roomId, eidStr, emoji]() {
                    client->sendReaction(roomId, eidStr, emoji);
                });
            }
        }
        e->accept(); return;
    }
    // Esc — close thread view
    if (e->key() == Qt::Key_Escape) {
        if (roomHandler_) roomHandler_->closeThreadView();
        timelineView_->clearSelection();
        e->accept(); return;
    }
    // Ctrl+L — focus room list
    if (e->key() == Qt::Key_L && (e->modifiers() & Qt::ControlModifier)) {
        roomList_->setFocus();
        e->accept(); return;
    }
    // Ctrl+Tab — next account (accounts only, skip separator/Add/Logout)
    if (e->key() == Qt::Key_Tab && (e->modifiers() & Qt::ControlModifier)) {
        if (accountSwitcher_) {
            int accountCount = accountSwitcher_->accountCount();
            if (accountCount <= 1) { e->accept(); return; }
            int next = accountCombo_->currentIndex() + 1;
            if (next >= accountCount) next = 0;
            accountCombo_->blockSignals(true);
            accountCombo_->setCurrentIndex(next);
            accountCombo_->blockSignals(false);
            accountSwitcher_->switchAccount(next);
        }
        e->accept(); return;
    }

    if (e->key() == Qt::Key_F11) { toolbarHandler_->doFullscreen(); e->accept(); return; }
    if (e->key() == Qt::Key_F12) {
        std::fprintf(stderr, "\n=== F12 DEBUG DUMP ===\n");
        std::fprintf(stderr, "roomModel rows: %d\n",
            roomModel_ ? roomModel_->rowCount() : -1);
        std::fprintf(stderr, "timelineModel rows: %d\n",
            timelineModel_ ? timelineModel_->rowCount() : -1);
        std::fprintf(stderr, "roomHandler currentRoom: %s\n",
            roomHandler_ && !roomHandler_->currentRoomId().empty()
                ? roomHandler_->currentRoomId().c_str() : "(none)");
        std::fprintf(stderr, "messageEdit visible: %d\n",
            messageEdit_ ? (int)messageEdit_->isVisible() : -1);
        std::fprintf(stderr, "timelineView visible: %d\n",
            timelineView_ ? (int)timelineView_->isVisible() : -1);
        std::fprintf(stderr, "placeholder visible: %d\n",
            timelinePlaceholder_ ? (int)timelinePlaceholder_->isVisible() : -1);
        std::fprintf(stderr, "========================\n\n");
        e->accept();
        return;
    }
    QMainWindow::keyPressEvent(e);
}

void MainWindow::onMessageClicked(const QString& eventId) {
    int row = timelineModel_->findRow(eventId.toStdString());
    if (row >= 0) {
        QModelIndex idx = timelineModel_->index(row);
        timelineView_->selectionModel()->select(idx, QItemSelectionModel::ClearAndSelect);
    }
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
} // namespace progressive::desktop
