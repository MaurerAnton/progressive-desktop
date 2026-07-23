// src/ui/main_window.cpp — Phase 3 full UI.
#include "main_window.hpp"
#include "handlers/toolbar_handler.hpp"
#include "handlers/room_handler.hpp"
#include "handlers/e2ee_init_handler.hpp"
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
#include <QApplication>
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

#include "handlers/sync_response_handler.hpp"
#include "handlers/attachment_handler.hpp"
#include "handlers/session_bootstrap.hpp"
#include "handlers/slash_command_handler.hpp"
#include "handlers/account_switcher.hpp"

#include <chrono>
#include <iostream>
#include <cstdio>

namespace progressive::desktop {

void MainWindow::setClient(std::shared_ptr<MatrixClient> client) {
    client_ = std::move(client);
    if (imageLoader_) imageLoader_->setClient(client_);
    if (chatView_) chatView_->setClient(client_);
    if (roomStore_) roomStore_->setClient(client_);
    if (toolbarHandler_) toolbarHandler_->setClient(client_);
    if (roomHandler_) roomHandler_->setClient(client_);
    if (auth_) auth_->setClient(client_);
    if (attachmentHandler_) attachmentHandler_->setClient(client_);
    if (accountSwitcher_) accountSwitcher_->setClient(client_);
    if (syncHandler_) syncHandler_->setClient(client_);
}

void MainWindow::setSessionStore(std::shared_ptr<SessionStore> store) {
    store_ = std::move(store);
    if (roomStore_) roomStore_->setSessionStore(store_);
}

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
        timelineModel_, statusLabel_, this);
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

    connect(threadBanner_, &QLabel::linkActivated, this, [this](const QString& link) {
        if (link == "back" && roomHandler_) roomHandler_->closeThreadView();
    });

    wireSyncCallbacks();

    chatView_ = new ChatView(client_, timelineModel_, messageEdit_, &sync_, this);
    auth_ = new AuthHandler(client_, store_, &sync_, userLabel_, statusLabel_, this);
    roomHandler_ = new RoomHandler(client_, roomStore_, roomModel_, timelineModel_,
        &sync_, imageLoader_, roomList_, timelineView_, statusLabel_, timelinePlaceholder_,
        loadMoreBtn_, chatLogBtn_, messageEdit_, QPointer<MainWindow>(this), this);

    connect(roomListDelegate_, &RoomListDelegate::inviteAccepted, roomHandler_, &RoomHandler::acceptInvite);
    connect(roomListDelegate_, &RoomListDelegate::inviteRejected, roomHandler_, &RoomHandler::rejectInvite);

    toolbarHandler_->setRoomHandler(roomHandler_);
    toolbarHandler_->setInterfaceElements(chatLogBtn_, threadBtn_);
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

    connect(timelineView_, &QListView::customContextMenuRequested, roomHandler_, &RoomHandler::onTimelineContextMenu);
    connect(timelineDelegate_, &TimelineDelegate::threadIndicatorClicked, roomHandler_, &RoomHandler::openThreadView);

    connect(logoutAction_, &QAction::triggered, auth_, &AuthHandler::logout);

    syncHandler_ = new SyncResponseHandler(client_, roomStore_, roomModel_,
        timelineModel_, &notifier_, roomListHeader_, inviteHeader_,
        statusLabel_, timelinePlaceholder_, timelineView_,
        messageEdit_, loadMoreBtn_, roomHandler_, this);
    attachmentHandler_ = new AttachmentHandler(client_, timelineModel_, statusLabel_, this);
    slashHandler_ = new SlashCommandHandler(timelineModel_, auth_, this);
    accountSwitcher_ = new AccountSwitcher(client_, store_, &sync_,
        accountCombo_, userLabel_, statusLabel_, roomModel_, timelineModel_,
        imageLoader_, timelineDelegate_, roomHandler_, chatView_,
        timelinePlaceholder_, timelineView_, messageEdit_, this);

    connect(accountCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) { accountSwitcher_->switchAccount(index); });

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
    E2eeInitHandler::persistCrypto(client_.get(), store_.get(), &sync_);
    sync_.stop();
    QMainWindow::closeEvent(e);
    QApplication::quit();
}

void MainWindow::keyPressEvent(QKeyEvent* e) {
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
