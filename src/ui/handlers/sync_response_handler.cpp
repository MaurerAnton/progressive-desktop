#include "sync_response_handler.hpp"
#include "core/matrix_client.hpp"
#include "core/thread_pool.hpp"
#include "core/memory_stats.hpp"
#include "../room/room_store.hpp"
#include "../room_list_model.hpp"
#include "../main_window.hpp"
#include "room_handler.hpp"
#include "../timeline/timeline_model.hpp"
#include <QLabel>
#include <QWidget>

namespace progressive::desktop {

SyncResponseHandler::SyncResponseHandler(MatrixClient* client, RoomStore* roomStore,
                        DesktopNotifier* notifier, QLabel* roomListHeader,
                        QPointer<MainWindow> mw, QObject* parent)
    : QObject(parent), client_(client), roomStore_(roomStore),
      notifier_(notifier), roomListHeader_(roomListHeader), mw_(mw) {}

void SyncResponseHandler::handle(FastSyncResponse resp) {
    bool hasData = !resp.joinedRooms.empty() || !resp.leftRoomIds.empty()
                   || !resp.invitedRooms.empty();

    if (!hasData || !roomStore_ || mw_.isNull()) return;

    mw_->statusLabel()->setText("Syncing...");
    QPointer<MainWindow> guard(mw_);
    std::string myUserId = client_ ? client_->account().userId : "";
    std::string curRoomId = mw_->roomHandler() ? mw_->roomHandler()->currentRoomId() : "";
    QPointer<RoomHandler> rmh(mw_->roomHandler());
    DesktopNotifier* notifier = notifier_;
    QLabel* rlh = roomListHeader_;

    ThreadPool::instance().enqueue([guard, rmh, resp = std::move(resp), myUserId, curRoomId, notifier, rlh]() mutable {
        auto syncUpdate = RoomStore::prepareRoomSyncUpdate(resp, curRoomId, myUserId);

        QMetaObject::invokeMethod(guard, [guard, rmh, syncUpdate = std::move(syncUpdate), notifier, rlh]() mutable {
            if (guard.isNull()) return;
            guard->roomStore()->applyRoomSyncUpdate(syncUpdate,
                guard->roomModel(), guard->timelineModel());

            for (const auto& rid : syncUpdate.roomsToRemove) {
                if (!rmh.isNull() && rid == rmh->currentRoomId()) {
                    guard->timelineModel()->clear();
                    rmh->clearCurrentRoom();
                    guard->timelineView()->hide();
                    guard->placeholder()->show();
                    guard->messageEdit()->hide();
                    if (guard->loadMoreBtn()) guard->loadMoreBtn()->hide();
                    break;
                }
            }

            if (syncUpdate.inviteCount > 0) {
                guard->inviteHead()->setText(syncUpdate.inviteText);
                guard->inviteHead()->show();
            } else {
                guard->inviteHead()->hide();
            }
            guard->roomModel()->updateHeader(rlh, guard->inviteHead());
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
            guard->roomStore()->batchLoadRoomStates(guard->roomModel(), QPointer<QWidget>(guard));

            guard->statusLabel()->setText(QString("Synced: %1 rooms | %2 messages")
                .arg(guard->roomModel()->rowCount()).arg(guard->timelineModel()->rowCount()));

            logMemorySnapshot("after-sync-cleanup");
            trimMemory();
        }, Qt::QueuedConnection);
    });

    static bool firstSync = true;
    if (firstSync) logMemorySnapshot("after-first-sync");
    firstSync = false;
}

} // namespace progressive::desktop
