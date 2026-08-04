#include "sync_response_handler.hpp"
#include "core/matrix_client.hpp"
#include "core/thread_pool.hpp"
#include "core/engine/sync_applier.hpp"
#include "core/fast_sync.hpp"
#include <simdjson.h>
#include "core/memory_stats.hpp"
#include "core/crypto/decryptor.hpp"
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

    ThreadPool::instance().enqueue([guard, rmh, resp = std::move(resp), myUserId, curRoomId, notifier]() mutable {
        auto keepAlive = std::make_shared<FastSyncResponse>(std::move(resp));
        auto syncUpdate = SyncApplier::prepareRoomSyncUpdate(*keepAlive, curRoomId, myUserId);
        if (guard && guard->decryptor_)
            guard->decryptor_->maybeReRequestKeys();  // worker thread, once per sync

        QMetaObject::invokeMethod(guard, [guard, rmh, syncUpdate = std::move(syncUpdate), notifier, keepAlive]() mutable {
            if (guard.isNull()) return;
            guard->roomStore_->applyRoomSyncUpdate(syncUpdate,
                guard->roomModel_, guard->timelineModel_, guard->decryptor_);

            if (guard->decryptor_) {
                // Core conversion (SyncApplier) + the UI-only model op.
                auto events = guard->decryptor_->takeDecryptedEvents();
                for (const auto& evt : events) {
                    simdjson::dom::parser p;
                    auto doc = p.parse(evt.plaintext);
                    if (doc.error() != simdjson::SUCCESS) continue;
                    std::string etype = "m.room.message";
                    auto t = doc.value()["type"].get_string();
                    if (t.error() == simdjson::SUCCESS) etype = std::string(t.value());
                    auto cr = doc.value()["content"];
                    std::string econtent = evt.plaintext;
                    if (cr.error() == simdjson::SUCCESS)
                        econtent = simdjson::to_string(cr.value());
                    FastEvent fe;
                    fe.eventId = std::string_view(evt.eventId);
                    fe.senderId = std::string_view(evt.senderId);
                    fe.type = etype;
                    fe.contentJson = econtent;
                    fe.originServerTs = evt.originServerTs;
                    DisplayedEvent de;
                    de.eventId = evt.eventId;
                    de.senderId = evt.senderId;
                    de.originServerTs = evt.originServerTs;
                    SyncApplier::fastEventToDisplayed(fe, de, evt.roomId, nullptr);
                    // Preserve the row's avatar (the freshly-converted event
                    // has none — the lookup happens in the sync apply).
                    {
                        int oldRow = guard->timelineModel_->findRow(evt.eventId);
                        const DisplayedEvent* oldEvt = oldRow >= 0 ? guard->timelineModel_->at(oldRow) : nullptr;
                        if (oldEvt && !oldEvt->avatarUrl.empty()) de.avatarUrl = oldEvt->avatarUrl;
                    }
                    guard->timelineModel_->replaceEvent(evt.eventId, de);
                }
            }

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

            guard->roomModel_->refreshHeader();
            logMemorySnapshot("after-rebuildRoomList");

            for (auto& rd : syncUpdate.roomsToUpsert) {
                if (rd.unreadCount == 0 && rd.highlightCount == 0) continue;
                if (rmh && !rmh.isNull() && rd.roomId == rmh->currentRoomId()) continue;
                QString body = rd.lastMessage.empty()
                    ? QString("New message") : QString::fromStdString(rd.lastMessage);
                if (rd.highlightCount > 0) {
                    body = QString("@mention in %1").arg(QString::fromStdString(rd.name));
                }
                notifier->notify(QString::fromStdString(rd.name), body);
                break;
            }
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
