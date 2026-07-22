// src/ui/ui_layout_builder.hpp
#pragma once
#include <QPointer>

class QSplitter;
class QToolBar;
class QLabel;
class QListView;
class QPushButton;
class QComboBox;
class QAction;
class QWidget;

namespace progressive::desktop {

class ImageLoader;
class RoomListModel;
class RoomListDelegate;
class TimelineModel;
class TimelineDelegate;
class MessageEdit;

struct UILayout {
    QSplitter* splitter = nullptr;
    QToolBar* toolbar = nullptr;
    QLabel* userLabel = nullptr;
    QLabel* statusLabel = nullptr;
    QLabel* roomListHeader = nullptr;
    QLabel* inviteHeader = nullptr;
    QListView* roomList = nullptr;
    RoomListDelegate* roomListDelegate = nullptr;
    QListView* timelineView = nullptr;
    QLabel* timelinePlaceholder = nullptr;
    QPushButton* loadMoreBtn = nullptr;
    QPushButton* chatLogBtn = nullptr;
    QPushButton* threadBtn = nullptr;
    QLabel* threadBanner = nullptr;
    MessageEdit* messageEdit = nullptr;
};

UILayout buildMainWindowLayout(QWidget* window,
    ImageLoader* imageLoader,
    RoomListModel* roomModel,
    TimelineModel* timelineModel,
    TimelineDelegate* timelineDelegate);

} // namespace progressive::desktop
