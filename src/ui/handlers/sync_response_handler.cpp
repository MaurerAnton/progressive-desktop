#include "sync_response_handler.hpp"
#include "core/matrix_client.hpp"
#include "core/thread_pool.hpp"
#include "core/memory_stats.hpp"
#include "../room/room_store.hpp"
#include "../room_list_model.hpp"
#include "../timeline/timeline_model.hpp"
#include "room_handler.hpp"

#include <QLabel>
#include <QPushButton>
#include <QWidget>

namespace progressive::desktop {

SyncResponseHandler::SyncResponseHandler(std::shared_ptr<MatrixClient> client, RoomStore* roomStore,
                         RoomListModel* roomModel, TimelineModel* timelineModel,
                         DesktopNotifier* notifier, QLabel* roomListHeader,
                         QLabel* inviteHeader, QLabel* statusLabel,
                         QWidget* placeholder, QWidget* timelineView,
                         QWidget* messageEdit, QPushButton* loadMoreBtn,
                         RoomHandler* roomHandler, QObject* parent)
    : QObject(parent), client_(std::move(client)), roomStore_(roomStore),
      roomModel_(roomModel), timelineModel_(timelineModel),
      notifier_(notifier), roomListHeader_(roomListHeader),
      inviteHeader_(inviteHeader), statusLabel_(statusLabel),
      placeholder_(placeholder), timelineView_(timelineView),
      messageEdit_(messageEdit), loadMoreBtn_(loadMoreBtn),
      roomHandler_(roomHandler) {
    syncLifeToken_ = std::make_shared<bool>(true);
}

SyncResponseHandler::~SyncResponseHandler() {
    if (syncLifeToken_) *syncLifeToken_ = false;
}

void SyncResponseHandler::handle(FastSyncResponse resp) {
    bool hasData = !resp.joinedRooms.empty() || !resp.leftRoomIds.empty()
                   || !resp.invitedRooms.empty();

    if (!hasData || !roomStore_) return;

    statusLabel_->setText("Syncing...");
    QPointer<SyncResponseHandler> guard(this);
    std::string myUserId = client_ ? client_->account().userId : "";
    std::string curRoomId = roomHandler_ ? roomHandler_->currentRoomId() : "";
    QPointer<RoomHandler> rmh(roomHandler_);
    DesktopNotifier* notifier = notifier_;
    QLabel* rlh = roomListHeader_;

    ThreadPool::instance().enqueue([guard, rmh, resp = std::move(resp), myUserId, curRoomId, notifier, rlh]() mutable {
        auto syncUpdate = RoomStore::prepareRoomSyncUpdate(resp, curRoomId, myUserId);

        QMetaObject::invokeMethod(guard, [guard, rmh, syncUpdate = std::move(syncUpdate), notifier, rlh]() mutable {
            if (guard.isNull()) return;
            guard->roomStore_->applyRoomSyncUpdate(syncUpdate,
                guard->roomModel_, guard->timelineModel_);

            for (const auto& rid : syncUpdate.roomsToRemove) {
                if (!rmh.isNull() && rid == rmh->currentRoomId()) {
                    guard->timelineModel_->clear();
                    rmh->clearCurrentRoom();
                    guard->timelineView_->hide();
                    guard->placeholder_->show();
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
            guard->roomModel_->updateHeader(rlh, guard->inviteHeader_);
            logMemorySnapshot("after-rebuildRoomList");

            static bool firstNotify = true;
            if (!firstNotify) {
                for (auto& rd : syncUpdate.roomsToUpsert) {
                    if (rd.highlightCount > 0) {
                        QString body = syncUpdate.lastNotificationBody.empty()
                            ? QString("Highlight!") : QString::fromStdString(syncUpdate.lastNotificationBody);
                        notifier->notify(QString::fromStdString(rd.name), body);
                        break;
                    }
                }
            }
            firstNotify = false;
            guard->roomStore_->batchLoadRoomStates(guard->roomModel_, guard->syncLifeToken_);

            guard->statusLabel_->setText(QString("Synced: %1 rooms | %2 messages")
                .arg(guard->roomModel_->joinedCount()).arg(guard->timelineModel_->rowCount()));

            logMemorySnapshot("after-sync-cleanup");
            trimMemory();
        }, Qt::QueuedConnection);
    });

    static bool firstSync = true;
    if (firstSync) logMemorySnapshot("after-first-sync");
    firstSync = false;
}

} // namespace progressive::desktop
