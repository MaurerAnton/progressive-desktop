// src/ui/room_handler.cpp — room interaction extracted from MainWindow.
// NOTE: Methods reference MainWindow members via mainWindow_-> pattern.
// Complete extraction requires QPointer<MainWindow> for safety.
#include "room_handler.hpp"
#include "main_window.hpp"
#include "room/room_store.hpp"
#include "room_list_model.hpp"
#include "timeline/timeline_model.hpp"
#include "timeline/timeline_handlers.hpp"
#include "core/matrix_client.hpp"

// Stub implementations — bodies extracted from main_window.cpp
// Wiring through MainWindow QPointer requires per-call-site adaptation

namespace progressive::desktop {

RoomHandler::RoomHandler(MatrixClient* client, RoomStore* roomStore,
                           RoomListModel* roomModel, TimelineModel* timelineModel,
                           SyncEngine* sync, ImageLoader* imageLoader,
                           QListView* roomList, QListView* timelineView,
                           QLabel* statusLabel, QLabel* timelinePlaceholder,
                           QPushButton* loadMoreBtn, QPushButton* chatLogBtn,
                           MessageEdit* messageEdit,
                           QPointer<MainWindow> mainWindow,
                           QObject* parent)
    : QObject(parent), client_(client), roomStore_(roomStore),
      roomModel_(roomModel), timelineModel_(timelineModel),
      sync_(sync), imageLoader_(imageLoader),
      roomList_(roomList), timelineView_(timelineView),
      statusLabel_(statusLabel), timelinePlaceholder_(timelinePlaceholder),
      loadMoreBtn_(loadMoreBtn), chatLogBtn_(chatLogBtn),
      messageEdit_(messageEdit), mainWindow_(mainWindow) {}

// Methods copied from MainWindow — replace 'this->' widget access with mainWindow_-> access
// TODO: complete extraction (see main_window.cpp for original implementations)

void RoomHandler::onRoomClicked(const QModelIndex&) {}
void RoomHandler::onRoomListContextMenu(const QPoint&) {}
void RoomHandler::onLoadMoreClicked() {}
void RoomHandler::onTimelineContextMenu(const QPoint&) {}
void RoomHandler::showTimelineContextMenu(const QString&, const QPoint&) {}
void RoomHandler::openThreadView(const QString&) {}
void RoomHandler::closeThreadView() {}

} // namespace progressive::desktop
