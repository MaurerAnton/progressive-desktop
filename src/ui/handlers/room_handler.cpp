// src/ui/room_handler.cpp — room interaction extracted from MainWindow.
#include "room_handler.hpp"
#include "thread_handler.hpp"
#include "room_context_menu.hpp"
#include "../main_window.hpp"
#include "../room/room_store.hpp"
#include "../room_list_model.hpp"
#include "../timeline/timeline_model.hpp"
#include "../timeline/timeline_handlers.hpp"
#include "core/matrix_client.hpp"
#include "../chat/chat_view.hpp"
#include "../dialogs/room_settings_dialog.hpp"
#include "../profile/room_members_dialog.hpp"

#include <QInputDialog>
#include <QMessageBox>
#include <QMenu>
#include <QCursor>
#include <QPointer>
#include <QDateTime>
#include <QStandardPaths>
#include <QDir>
#include <QApplication>
#include "core/thread_pool.hpp"

#include <simdjson.h>

namespace progressive::desktop {

namespace {


std::string extractThreadRootId(std::string_view contentJson) {
    auto relPos = contentJson.find("\"m.relates_to\"");
    if (relPos == std::string_view::npos) return {};
    auto objStart = contentJson.find('{', relPos);
    if (objStart == std::string_view::npos) return {};
    int depth = 0;
    size_t objEnd = objStart;
    for (; objEnd < contentJson.size(); ++objEnd) {
        if (contentJson[objEnd] == '{') depth++;
        else if (contentJson[objEnd] == '}') { depth--; if (depth == 0) { objEnd++; break; } }
    }
    std::string_view relObj = contentJson.substr(objStart, objEnd - objStart);

    auto rtPos = relObj.find("\"rel_type\":\"m.thread\"");
    if (rtPos == std::string_view::npos) {
        rtPos = relObj.find("\"rel_type\": \"m.thread\"");
    }
    if (rtPos == std::string_view::npos) return {};

    auto eidPos = relObj.find("\"event_id\":\"");
    if (eidPos == std::string_view::npos) {
        eidPos = relObj.find("\"event_id\": \"");
        if (eidPos == std::string_view::npos) return {};
        eidPos += 13;
    } else {
        eidPos += 12;
    }
    auto eidEnd = relObj.find('"', eidPos);
    if (eidEnd == std::string_view::npos) return {};
    return std::string(relObj.substr(eidPos, eidEnd - eidPos));
}

std::string extractReplyToId(std::string_view contentJson) {
    auto relPos = contentJson.find("\"m.in_reply_to\"");
    if (relPos == std::string_view::npos) return {};
    auto objStart = contentJson.find('{', relPos);
    if (objStart == std::string_view::npos) return {};
    int depth = 0;
    size_t objEnd = objStart;
    for (; objEnd < contentJson.size(); ++objEnd) {
        if (contentJson[objEnd] == '{') depth++;
        else if (contentJson[objEnd] == '}') { depth--; if (depth == 0) { objEnd++; break; } }
    }
    std::string_view obj = contentJson.substr(objStart, objEnd - objStart);
    auto eidPos = obj.find("\"event_id\":\"");
    if (eidPos == std::string_view::npos) {
        eidPos = obj.find("\"event_id\": \"");
        if (eidPos == std::string_view::npos) return {};
        eidPos += 13;
    } else {
        eidPos += 12;
    }
    auto eidEnd = obj.find('"', eidPos);
    if (eidEnd == std::string_view::npos) return {};
    return std::string(obj.substr(eidPos, eidEnd - eidPos));
}

} // namespace

RoomHandler::RoomHandler(std::shared_ptr<MatrixClient> client, RoomStore* roomStore,
                           RoomListModel* roomModel, TimelineModel* timelineModel,
                           SyncEngine* sync, ImageLoader* imageLoader,
                           QListView* roomList, QListView* timelineView,
                           QLabel* statusLabel, QLabel* timelinePlaceholder,
                           QPushButton* loadMoreBtn, QPushButton* chatLogBtn,
                           MessageEdit* messageEdit,
                           QPointer<MainWindow> mainWindow,
                           QObject* parent)
    : QObject(parent), client_(std::move(client)), roomStore_(roomStore),
      roomModel_(roomModel), timelineModel_(timelineModel),
      sync_(sync), imageLoader_(imageLoader),
      roomList_(roomList), timelineView_(timelineView),
      statusLabel_(statusLabel), timelinePlaceholder_(timelinePlaceholder),
      loadMoreBtn_(loadMoreBtn), chatLogBtn_(chatLogBtn),
      messageEdit_(messageEdit), mainWindow_(mainWindow) {
    roomLifeToken_ = std::make_shared<bool>(true);
    threadHandler_ = new ThreadHandler(client_, timelineModel_,
        mainWindow_->threadBanner(), statusLabel_, mainWindow_, this);
    contextMenu_ = new RoomContextMenu(client_, timelineModel_, roomModel_,
        roomList_, threadHandler_, statusLabel_, mainWindow_, this);
    connect(contextMenu_, &RoomContextMenu::roomLeft, this, [this](const std::string& roomId) {
        if (currentRoomIdStr_ == roomId) { chatLogging_ = false; chatLogFile_.reset(); }
    });
}

RoomHandler::~RoomHandler() {
    if (roomLifeToken_) *roomLifeToken_ = false;
}

void RoomHandler::setClient(std::shared_ptr<MatrixClient> c) {
    client_ = std::move(c);
    if (threadHandler_) threadHandler_->setClient(client_);
    if (contextMenu_) contextMenu_->setClient(client_);
}

void RoomHandler::onRoomClicked(const QModelIndex& idx) {
    const RoomData* r = roomModel_->at(idx.row());
    if (!r) return;
    if (mainWindow_.isNull()) return;

    currentRoomIdStr_ = r->roomId;
    threadHandler_->clearThreadRoot();
    mainWindow_->threadBanner()->hide();
    currentPrevBatch_.clear();
    chatLogging_ = false;
    chatLogBtn_->setChecked(false);
    chatLogFile_.reset();
    mainWindow_->chatView()->setCurrentRoom(r->roomId, threadHandler_->currentThreadRoot(), r->isEncrypted);
    timelineModel_->clear();
    timelinePlaceholder_->hide();
    timelineView_->show();
    messageEdit_->show();
    loadMoreBtn_->hide();
    chatLogBtn_->show();
    mainWindow_->threadBtn()->show();
    loadMoreBtn_->parentWidget()->show();
    messageEdit_->setFocus();
    mainWindow_->setWindowTitle(QString("Progressive Chat — %1").arg(QString::fromStdString(r->name)));

    if (client_ && client_->isLoggedIn()) {
        int lastRow = timelineModel_->rowCount(QModelIndex()) - 1;
        if (lastRow >= 0) {
            auto* evt = timelineModel_->at(lastRow);
            if (evt && !evt->eventId.empty()) {
                std::string roomIdStr = r->roomId;
                std::string eventIdStr = evt->eventId;
                auto client = client_;
                ThreadPool::instance().enqueue([client, roomIdStr, eventIdStr]() {
                    client->setReadMarker(roomIdStr, eventIdStr);
                });
            }
        }
    }

    std::vector<std::string> senderIds;
    for (int i = 0; i < timelineModel_->rowCount(QModelIndex()); ++i) {
        auto* evt = timelineModel_->at(i);
        if (evt && !evt->senderId.empty())
            senderIds.push_back(evt->senderId);
    }
    roomStore_->loadMembers(r->roomId, roomLifeToken_, senderIds,
        [this](std::vector<MemberInfo> members) {
            for (const auto& m : members) {
                if (!m.avatarUrl.empty()) memberAvatarCache_[m.userId] = m.avatarUrl;
                if (!m.displayName.empty()) memberAvatarCache_[m.userId + "/name"] = m.displayName;
            }
            for (int i = 0; i < mainWindow_->timelineModel()->rowCount(); ++i) {
                auto* evt = mainWindow_->timelineModel()->at(i);
                if (!evt || !evt->avatarUrl.empty() || evt->senderId.empty()) continue;
                auto avIt = memberAvatarCache_.find(evt->senderId);
                if (avIt != memberAvatarCache_.end()) {
                    evt->avatarUrl = avIt->second;
                    auto idx = mainWindow_->timelineModel()->index(i);
                    emit mainWindow_->timelineModel()->dataChanged(idx, idx, {TimelineModel::AvatarUrlRole});
                }
            }
        });

    QStringList memberNames;
    for (const auto& [key, val] : memberAvatarCache_) {
        if (key.find("/name") != std::string::npos) {
            memberNames << QString::fromStdString(val);
        }
    }
    messageEdit_->setMembers(memberNames);

    roomStore_->loadHistory(r->roomId, timelineModel_,
        roomLifeToken_, [this](int count, const std::string& prevBatch) {
            if (count > 0) {
                currentPrevBatch_ = prevBatch;
                if (loadMoreBtn_ && !prevBatch.empty()) loadMoreBtn_->show();
                timelineView_->scrollToBottom();
                statusLabel_->setText(QString("Loaded %1 messages.").arg(count));
            }
        });
}

void RoomHandler::onRoomListContextMenu(const QPoint& pos) {
    contextMenu_->onRoomListContextMenu(pos, currentRoomIdStr_);
}

void RoomHandler::onLoadMoreClicked() {
    if (currentRoomIdStr_.empty() || !client_ || currentPrevBatch_.empty()) return;
    if (mainWindow_.isNull()) return;

    statusLabel_->setText("Loading older messages...");
    loadMoreBtn_->setEnabled(false);

    std::string roomId = currentRoomIdStr_;
    std::string from = currentPrevBatch_;
    auto client = client_;
    QPointer<MainWindow> guard(mainWindow_);
    QPointer<RoomHandler> self(this);

    ThreadPool::instance().enqueue([guard, self, client, roomId, from]() {
        auto result = client->getMessages(roomId, from, 30);
        QMetaObject::invokeMethod(guard, [guard, self, result]() {
            if (guard.isNull() || self.isNull()) return;
            if (!result.ok) {
                self->statusLabel_->setText("Failed to load older messages.");
                self->loadMoreBtn_->setEnabled(true);
                return;
            }

            simdjson::dom::parser parser;
            auto rootResult = parser.parse(result.data);
            if (rootResult.error() != simdjson::SUCCESS) {
                self->statusLabel_->setText("Failed to parse older messages.");
                self->loadMoreBtn_->setEnabled(true);
                return;
            }

            auto endToken = rootResult.value()["end"].get_string();
            if (endToken.error() == simdjson::SUCCESS && endToken.value().size() > 0) {
                self->currentPrevBatch_ = std::string(endToken.value());
            } else {
                auto startToken = rootResult.value()["start"].get_string();
                if (startToken.error() == simdjson::SUCCESS) {
                    self->currentPrevBatch_ = std::string(startToken.value());
                } else {
                    self->currentPrevBatch_.clear();
                }
            }

            auto chunkResult = rootResult.value()["chunk"].get_array();
            if (chunkResult.error() != simdjson::SUCCESS) {
                self->statusLabel_->setText("No older messages.");
                self->loadMoreBtn_->setEnabled(true);
                return;
            }

            std::vector<DisplayedEvent> events;
            for (auto evt : chunkResult.value()) {
                DisplayedEvent de;
                auto t = evt["type"].get_string();
                if (t.error() == simdjson::SUCCESS) de.type = std::string(t.value());
                auto eid = evt["event_id"].get_string();
                if (eid.error() == simdjson::SUCCESS) de.eventId = std::string(eid.value());
                auto sender = evt["sender"].get_string();
                if (sender.error() == simdjson::SUCCESS) de.senderId = std::string(sender.value());
                auto ts = evt["origin_server_ts"].get_int64();
                if (ts.error() == simdjson::SUCCESS) de.originServerTs = ts.value();
                auto contentResult = evt["content"];
                if (contentResult.error() == simdjson::SUCCESS) de.contentJson = simdjson::to_string(contentResult.value());

                if (!de.senderId.empty() && de.senderId[0] == '@') {
                    auto colon = de.senderId.find(':');
                    if (colon != std::string::npos) de.senderName = de.senderId.substr(1, colon - 1);
                    else de.senderName = de.senderId.substr(1);
                }
                if (de.type == "m.room.message") {
                    de.msgtype = extractStringDec(de.contentJson, "msgtype");
                    de.body = extractStringDec(de.contentJson, "body");
                    if (de.msgtype == "m.image" || de.msgtype == "m.video") {
                        de.mxcUrl = extractStringDec(de.contentJson, "url");
                        de.mimetype = extractStringDec(de.contentJson, "mimetype");
                    }
                    std::string_view cv(de.contentJson);
                    std::string threadRoot = extractThreadRootId(cv);
                    if (!threadRoot.empty()) {
                        de.isThreadReply = true;
                        de.threadRootId = threadRoot;
                    }
                    std::string replyTo = extractReplyToId(cv);
                    if (!replyTo.empty()) {
                        de.isReply = true;
                        de.replyToEventId = replyTo;
                    }
                }
                auto it = self->memberAvatarCache_.find(de.senderId);
                if (it != self->memberAvatarCache_.end()) de.avatarUrl = it->second;
                auto nameIt = self->memberAvatarCache_.find(de.senderId + "/name");
                if (nameIt != self->memberAvatarCache_.end()) de.senderName = nameIt->second;

                events.push_back(std::move(de));
            }

            std::reverse(events.begin(), events.end());
            self->timelineModel_->appendFront(events);

            if (self->currentPrevBatch_.empty()) {
                self->loadMoreBtn_->hide();
            }
            self->loadMoreBtn_->setEnabled(true);
            self->statusLabel_->setText(QString("Loaded %1 older message(s).").arg(events.size()));
        }, Qt::QueuedConnection);
    });
}

void RoomHandler::onTimelineContextMenu(const QPoint& pos) {
    auto idx = timelineView_->indexAt(pos);
    if (!idx.isValid()) return;
    QString eventId = idx.data(TimelineModel::EventIdRole).toString();
    if (eventId.isEmpty()) return;
    auto globalPos = timelineView_->mapToGlobal(pos);
    contextMenu_->showTimelineContextMenu(eventId, globalPos, currentRoomIdStr_);
}

void RoomHandler::openThreadView(const QString& rootEventId) {
    threadHandler_->openThreadView(rootEventId, currentRoomIdStr_);
}

void RoomHandler::closeThreadView() {
    threadHandler_->closeThreadView(currentRoomIdStr_);
    if (!currentRoomIdStr_.empty()) {
        roomStore_->loadHistory(currentRoomIdStr_, timelineModel_,
            roomLifeToken_, [this](int count, const std::string& pb) {
                if (count > 0) {
                    currentPrevBatch_ = pb;
                    if (!pb.empty() && loadMoreBtn_) loadMoreBtn_->show();
                    timelineView_->scrollToBottom();
                }
            });
    }
}

void RoomHandler::acceptInvite(const QString& roomId) {
    if (!client_) return;
    statusLabel_->setText("Accepting invite...");
    auto client = client_;
    QPointer<RoomHandler> self(this);
    ThreadPool::instance().enqueue([self, client, roomId]() {
        auto r = client->joinRoom(roomId.toStdString());
        QMetaObject::invokeMethod(self, [self, r, roomId]() {
            if (self.isNull()) return;
            if (r.ok) {
                RoomData* rd = const_cast<RoomData*>(self->roomModel_->at(
                    self->roomModel_->findRowByRoomId(roomId.toStdString())));
                if (rd) { rd->isInvite = false;
                    int row = self->roomModel_->findRowByRoomId(roomId.toStdString());
                    if (row >= 0) emit self->roomModel_->dataChanged(
                        self->roomModel_->index(row), self->roomModel_->index(row)); }
                self->statusLabel_->setText("Joined room.");
            } else {
                self->statusLabel_->setText("Failed: " + QString::fromStdString(r.error.message));
            }
        }, Qt::QueuedConnection);
    });
}

void RoomHandler::rejectInvite(const QString& roomId) {
    if (!client_) return;
    statusLabel_->setText("Rejecting invite...");
    auto client = client_;
    QPointer<RoomHandler> self(this);
    ThreadPool::instance().enqueue([self, client, roomId]() {
        auto r = client->leaveRoom(roomId.toStdString());
        QMetaObject::invokeMethod(self, [self, r, roomId]() {
            if (self.isNull()) return;
            if (r.ok) { self->roomModel_->removeRoom(roomId.toStdString());
                self->statusLabel_->setText("Invite rejected."); }
            else self->statusLabel_->setText("Failed: " + QString::fromStdString(r.error.message));
        }, Qt::QueuedConnection);
    });
}

} // namespace progressive::desktop
