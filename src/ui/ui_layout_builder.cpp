// src/ui/ui_layout_builder.cpp
#include "ui_layout_builder.hpp"
#include "shared/image_loader.hpp"
#include "room_list_model.hpp"
#include "room_list_delegate.hpp"
#include "timeline/timeline_model.hpp"
#include "timeline/timeline_delegate.hpp"
#include "chat/message_edit.hpp"

#include <QMainWindow>
#include <QToolBar>
#include <QStatusBar>
#include <QSplitter>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QListView>
#include <QPushButton>
#include <QComboBox>
#include <QAction>
#include <QWidget>
#include <QSizePolicy>
#include <QAbstractItemView>

namespace progressive::desktop {

namespace {
inline constexpr int kRoomListMinW = 280;
inline constexpr int kRoomListMaxW = 400;
inline constexpr int kToolbarSpacing = 4;
inline constexpr int kSplitterLeft   = 1;
inline constexpr int kSplitterRight  = 4;
} // namespace

UILayout buildMainWindowLayout(QWidget* window,
    ImageLoader* imageLoader,
    RoomListModel* roomModel,
    TimelineModel* timelineModel,
    TimelineDelegate* timelineDelegate) {

    auto* mw = qobject_cast<QMainWindow*>(window);
    UILayout ui;

    // --- Toolbar ---
    ui.toolbar = mw ? mw->addToolBar("Main") : new QToolBar(window);
    ui.toolbar->setMovable(false);

    ui.userLabel = new QLabel(" Not logged in ", window);
    ui.toolbar->addWidget(ui.userLabel);
    ui.toolbar->addSeparator();

    // --- Splitter ---
    ui.splitter = new QSplitter(Qt::Horizontal, window);

    auto* leftPanel = new QWidget(ui.splitter);
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);

    ui.roomListHeader = new QLabel(" Chats (0) ", window);
    ui.roomListHeader->setStyleSheet("font-weight:600; padding:10px 12px; color:#e8e8e8; background:#1e1e1e;");
    leftLayout->addWidget(ui.roomListHeader);

    ui.inviteHeader = new QLabel("  Invitations (0) ", window);
    ui.inviteHeader->setStyleSheet("font-weight:600; padding:6px 12px; color:#ff9944; background:#2a1e1e;");
    ui.inviteHeader->hide();
    leftLayout->addWidget(ui.inviteHeader);

    ui.roomList = new QListView(leftPanel);
    ui.roomList->setModel(roomModel);
    ui.roomList->setStyleSheet(
        "QListView{background:#1e1e1e;border:none;}"
        "QListView::item:hover{background:#2a2a3e;}"
        "QListView::item:selected{background:#3a3a5e;}");
    ui.roomListDelegate = new RoomListDelegate(imageLoader, ui.roomList);
    ui.roomList->setItemDelegate(ui.roomListDelegate);
    ui.roomList->setMinimumWidth(kRoomListMinW);
    ui.roomList->setMaximumWidth(kRoomListMaxW);
    ui.roomList->setAlternatingRowColors(false);
    ui.roomList->setUniformItemSizes(true);
    ui.roomList->setWordWrap(false);
    leftLayout->addWidget(ui.roomList);

    // Right: timeline + input
    auto* rightPanel = new QWidget(ui.splitter);
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);

    ui.timelineView = new QListView(rightPanel);
    ui.timelineView->setModel(timelineModel);
    ui.timelineView->setItemDelegate(timelineDelegate);
    ui.timelineView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    ui.timelineView->setUniformItemSizes(false);
    ui.timelineView->setContextMenuPolicy(Qt::CustomContextMenu);
    ui.timelineView->viewport()->setContentsMargins(0, 0, 0, 12);
    ui.timelineView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    ui.threadBanner = new QLabel(
        "<a href='back' style='color:#6699cc'> Back to chat</a>", rightPanel);
    ui.threadBanner->setStyleSheet("background:#2a2a3a; padding:8px; color:#cccccc;");
    ui.threadBanner->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    ui.threadBanner->hide();
    rightLayout->addWidget(ui.threadBanner);

    auto* timelineToolbar = new QWidget(rightPanel);
    auto* timelineToolbarLayout = new QHBoxLayout(timelineToolbar);
    timelineToolbarLayout->setContentsMargins(4, 2, 4, 2);
    timelineToolbarLayout->setSpacing(4);

    ui.loadMoreBtn = new QPushButton(" Load older", rightPanel);
    ui.loadMoreBtn->setStyleSheet(
        "QPushButton{background:#2a2a3a;color:#888;border:1px solid #3a3a3a;"
        "padding:2px 10px;border-radius:3px;font-size:11px;}"
        "QPushButton:hover{color:#ccc;border-color:#555;}");
    ui.loadMoreBtn->hide();

    ui.chatLogBtn = new QPushButton(" Save", rightPanel);
    ui.chatLogBtn->setCheckable(true);
    ui.chatLogBtn->setStyleSheet(
        "QPushButton{background:#2a2a3a;color:#888;border:1px solid #3a3a3a;"
        "padding:2px 10px;border-radius:3px;font-size:11px;}"
        "QPushButton:checked{color:#6c6;border-color:#4a6;}"
        "QPushButton:hover{color:#ccc;}");
    ui.chatLogBtn->hide();

    ui.threadBtn = new QPushButton(" Threads", rightPanel);
    ui.threadBtn->setStyleSheet(
        "QPushButton{background:#2a2a3a;color:#888;border:1px solid #3a3a3a;"
        "padding:2px 10px;border-radius:3px;font-size:11px;}"
        "QPushButton:hover{color:#ccc;border-color:#555;}");
    ui.threadBtn->hide();

    timelineToolbarLayout->addWidget(ui.loadMoreBtn);
    timelineToolbarLayout->addWidget(ui.chatLogBtn);
    timelineToolbarLayout->addStretch();
    timelineToolbarLayout->addWidget(ui.threadBtn);
    rightLayout->addWidget(timelineToolbar, 0);
    timelineToolbar->hide();

    rightLayout->addWidget(ui.timelineView, 1);

    ui.timelinePlaceholder = new QLabel(
        "Select a chat from the list\nor click \"+ New chat\" to start a conversation", window);
    ui.timelinePlaceholder->setAlignment(Qt::AlignCenter);
    ui.timelinePlaceholder->setStyleSheet("color:#969696; font-size:14pt; background:#141414;");
    ui.timelinePlaceholder->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    rightLayout->addWidget(ui.timelinePlaceholder, 1);
    ui.timelinePlaceholder->show();
    ui.timelineView->hide();

    ui.messageEdit = new MessageEdit(rightPanel);
    ui.messageEdit->hide();
    ui.messageEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    rightLayout->addWidget(ui.messageEdit, 0, Qt::AlignBottom);

    ui.splitter->addWidget(leftPanel);
    ui.splitter->addWidget(rightPanel);
    ui.splitter->setStretchFactor(0, 1);
    ui.splitter->setStretchFactor(1, 4);

    if (mw) mw->setCentralWidget(ui.splitter);

    ui.statusLabel = new QLabel("Not synced yet.", window);
    if (mw) mw->statusBar()->addWidget(ui.statusLabel, 1);

    return ui;
}

} // namespace progressive::desktop
