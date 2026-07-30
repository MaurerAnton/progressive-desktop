// src/ui/handlers/thread_handler.cpp — thread view management.
#include "thread_handler.hpp"
#include "core/matrix_client.hpp"
#include "core/thread_pool.hpp"
#include "core/crypto/decryptor.hpp"
#include "core/sync_engine.hpp"
#include "../timeline/timeline_model.hpp"
#include "../room/room_store.hpp"
#include "../room/event_parser.hpp"
#include "../room_list_model.hpp"
#include "../main_window.hpp"
#include "core/debug_log.hpp"
#include "room_key_helper.hpp"

#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QPointer>
#include <simdjson.h>
#include <chrono>

namespace progressive::desktop {

inline constexpr int kTruncLen = 100;

ThreadHandler::ThreadHandler(std::shared_ptr<MatrixClient> client, TimelineModel* timelineModel,
                               QLabel* threadBanner, QLabel* statusLabel,
                               QPointer<MainWindow> mw,
                               RoomListModel* roomModel, SyncEngine* sync,
                               QObject* parent)
    : QObject(parent), client_(std::move(client)), timelineModel_(timelineModel),
      threadBanner_(threadBanner), statusLabel_(statusLabel), mw_(mw),
      roomModel_(roomModel), sync_(sync) {}

void ThreadHandler::openThreadView(const QString& rootEventId, const std::string& roomId) {
    if (!client_ || roomId.empty()) return;
    if (mw_.isNull()) return;

    currentThreadRoot_ = rootEventId.toStdString();

    // Snapshot root before clear() so fallback branches in the async callback
    // can find the decrypted body/msgtype from the local timeline.
    DisplayedEvent rootSnapshot;
    bool hasRootSnapshot = false;
    int rootRow = timelineModel_->findRow(rootEventId.toStdString());
    if (rootRow >= 0) {
        auto* local = timelineModel_->at(rootRow);
        if (local) { rootSnapshot = *local; hasRootSnapshot = true; }
    }
    // Snapshot thread replies from local timeline before clearing
    std::vector<DisplayedEvent> replySnapshots;
    for (int i = 0; i < timelineModel_->rowCount(QModelIndex()); i++) {
        auto* evt = timelineModel_->at(i);
        if (evt && evt->isThreadReply && evt->threadRootId == currentThreadRoot_) {
            replySnapshots.push_back(*evt);
        }
    }
    timelineModel_->clear();
    threadBanner_->show();
    statusLabel_->setText("Loading thread...");

    std::string rootEid = rootEventId.toStdString();
    QPointer<MainWindow> guard(mw_);
    QPointer<ThreadHandler> self(this);

    ThreadPool::instance().enqueue([guard, self, roomId, rootEid, rootSnapshot, hasRootSnapshot, replySnapshots]() {
        auto r = self->client_->getThreadReplies(roomId, rootEid);
        QMetaObject::invokeMethod(guard, [guard, self, r, rootEid, rootSnapshot, hasRootSnapshot, replySnapshots]() {
            if (guard.isNull() || self.isNull()) return;
            if (!r.ok) {
                self->statusLabel_->setText("Failed to load thread: " + QString::fromStdString(r.error.message));
                return;
            }
            simdjson::dom::parser parser;
            auto rootResult = parser.parse(r.data);
            if (rootResult.error() != simdjson::SUCCESS) {
                self->statusLabel_->setText("Failed to parse thread replies.");
                return;
            }

            auto origResult = rootResult.value()["original_event"];
            LOG(LogChannel::E2EE, "thread: origResult error=%d", (int)origResult.error());
            if (origResult.error() == simdjson::SUCCESS) {
                DisplayedEvent root;
                parseEventFields(origResult.value(), root);
                if (root.type == "m.room.message") {
                    root.msgtype = msgType(root.contentJson);
                    root.body = msgBody(root.contentJson);
                    if (root.msgtype == "m.image" || root.msgtype == "m.video") {
                        root.mxcUrl = extractStringDec(root.contentJson, "url");
                    }
                } else if (root.type == "m.room.encrypted") {
                    // DEBT(B41): pass Decryptor* for full E2EE thread root support
                    if (hasRootSnapshot) {
                        root.body = rootSnapshot.body;
                        root.msgtype = rootSnapshot.msgtype;
                    }
                }
                root.isThreadRoot = true;
                self->timelineModel_->appendBack(root);
            } else {
                if (hasRootSnapshot) {
                    DisplayedEvent root = rootSnapshot; root.isThreadRoot = true;
                    self->timelineModel_->appendBack(root);
                }
            }

            auto chunkResult = rootResult.value()["chunk"].get_array();
            if (chunkResult.error() != simdjson::SUCCESS) {
                self->statusLabel_->setText("No thread replies found.");
                return;
            }
            std::vector<DisplayedEvent> events;
            for (auto evt : chunkResult.value()) {
                DisplayedEvent de;
                parseEventFields(evt, de);
                if (de.type == "m.room.message") {
                    de.msgtype = msgType(de.contentJson);
                    de.body = msgBody(de.contentJson);
                    if (de.msgtype == "m.image" || de.msgtype == "m.video") {
                        de.mxcUrl = extractStringDec(de.contentJson, "url");
                    }
                }
                de.isThreadReply = true;
                events.push_back(std::move(de));
            }
            for (const auto& de : events)
                self->timelineModel_->appendBack(de);
            for (const auto& de : replySnapshots)
                self->timelineModel_->appendBack(de);
            self->statusLabel_->setText(QString("Loaded %1 thread reply(s).").arg(events.size()));
        }, Qt::QueuedConnection);
    });
}

void ThreadHandler::closeThreadView(const std::string& roomId) {
    currentThreadRoot_.clear();
    threadBanner_->hide();
    timelineModel_->clear();
}

void ThreadHandler::replyInThread(const QString& eventId, const std::string& roomId) {
    if (!client_ || mw_.isNull() || roomId.empty()) return;
    QString rootText;
    int row = timelineModel_->findRow(eventId.toStdString());
    if (row >= 0) {
        auto* evt = timelineModel_->at(row);
        if (evt) rootText = QString::fromStdString(evt->body);
    }
    bool ok;
    QString reply = QInputDialog::getText(mw_.data(), "Reply in thread",
        QString("Replying to:\n\"%1\"\n\nYour reply:").arg(rootText.left(kTruncLen)),
        QLineEdit::Normal, "", &ok);
    if (!ok || reply.trimmed().isEmpty()) return;
    sendThreadReply(roomId, currentThreadRoot_, eventId.toStdString(), reply.toStdString());
}

void ThreadHandler::sendThreadReply(const std::string& roomId,
                                      const std::string& threadRoot,
                                      const std::string& replyToEventId,
                                      const std::string& text) {
    std::string effectiveRoot = threadRoot.empty() ? replyToEventId : threadRoot;
    bool isEncrypted = false;
    if (roomModel_) {
        int row = roomModel_->findRowByRoomId(roomId);
        if (row >= 0) {
            auto* rd = roomModel_->at(row);
            if (rd) isEncrypted = rd->isEncrypted;
        }
    }
    QPointer<MainWindow> guard(mw_);
    QPointer<ThreadHandler> self(this);
    ThreadPool::instance().enqueue([guard, self, roomId, effectiveRoot, text, isEncrypted]() {
        if (!isEncrypted) {
            LOG(LogChannel::GUI, "sendThreadReply: room=%.30s root=%.30s textLen=%zu",
                roomId.c_str(), effectiveRoot.c_str(), text.size());
            auto r = self->client_->sendThreadReply(roomId, text, effectiveRoot);
            LOG(LogChannel::GUI, "sendThreadReply: http ok=%d httpStatus=%d data=%s err=%s",
                r.ok ? 1 : 0, r.httpStatus, r.data.c_str(),
                r.error.message.c_str());
            QMetaObject::invokeMethod(guard, [guard, self, r, effectiveRoot, text]() {
                if (guard.isNull() || self.isNull()) return;
                if (r.ok) {
                    DisplayedEvent echo;
                    echo.eventId = r.data;
                    echo.senderId = self->client_->account().userId;
                    if (!echo.senderId.empty() && echo.senderId[0] == '@') {
                        auto colon = echo.senderId.find(':');
                        echo.senderName = (colon != std::string::npos) ? echo.senderId.substr(1, colon - 1) : echo.senderId.substr(1);
                    }
                    echo.originServerTs = static_cast<int64_t>(
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count());
                    echo.type = "m.room.message";
                    echo.msgtype = "m.text";
                    echo.body = text;
                     echo.isThreadReply = true;
                     echo.threadRootId = effectiveRoot;
                     self->timelineModel_->appendBack(echo);
                     LOG(LogChannel::GUI, "sendThreadReply: echo appended eventId=%s",
                         echo.eventId.c_str());
                 }
            }, Qt::QueuedConnection);
        } else {
            auto* dec = self->sync_ ? self->sync_->decryptor() : nullptr;
            if (!dec || !dec->isInitialized()) {
                LOG(LogChannel::E2EE, "sendThreadReply: decryptor not init, "
                    "FALLING BACK to unencrypted — SECURITY DEBT");
                auto r = self->client_->sendThreadReply(roomId, text, effectiveRoot);
                QMetaObject::invokeMethod(guard, [guard, self, r, effectiveRoot, text]() {
                    if (guard.isNull() || self.isNull()) return;
                    if (r.ok) {
                        DisplayedEvent echo;
                        echo.eventId = r.data;
                        echo.senderId = self->client_->account().userId;
                        if (!echo.senderId.empty() && echo.senderId[0] == '@') {
                            auto colon = echo.senderId.find(':');
                            echo.senderName = (colon != std::string::npos) ? echo.senderId.substr(1, colon - 1) : echo.senderId.substr(1);
                        }
                        echo.originServerTs = static_cast<int64_t>(
                            std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch()).count());
                        echo.type = "m.room.message";
                        echo.msgtype = "m.text";
                        echo.body = text;
                        echo.isThreadReply = true;
                        echo.threadRootId = effectiveRoot;
                        self->timelineModel_->appendBack(echo);
                    }
                }, Qt::QueuedConnection);
                return;
            }
            std::string deviceId = self->client_->account().deviceId;
            std::string sessId = dec->getOrCreateOutboundSession(roomId);
            if (sessId.empty()) {
                QMetaObject::invokeMethod(guard, [guard, self]() {
                    if (guard.isNull() || self.isNull()) return;
                    self->statusLabel_->setText("�� Failed to encrypt thread reply");
                }, Qt::QueuedConnection);
                return;
            }
            std::string escaped;
            for (char c : text) {
                if (c == '"') escaped += "\\\"";
                else if (c == '\\') escaped += "\\\\";
                else if (c == '\n') escaped += "\\n";
                else escaped += c;
            }
            std::string relatesTo = ",\"m.relates_to\":{\"rel_type\":\"m.thread\",\"event_id\":\""
                                  + effectiveRoot + "\"}";
            std::string inner = "{\"type\":\"m.room.message\",\"content\":{\"msgtype\":\"m.text\",\"body\":\""
                              + escaped + "\"" + relatesTo + "},\"room_id\":\""
                              + roomId + "\"}";
            std::string enc = dec->encryptMessage(roomId, deviceId, inner);
            if (enc.empty()) {
                QMetaObject::invokeMethod(guard, [guard, self]() {
                    if (guard.isNull() || self.isNull()) return;
                    self->statusLabel_->setText("�� Encryption failed for thread reply");
                }, Qt::QueuedConnection);
                return;
            }
            shareRoomKeyForRoom(*self->client_, *dec, roomId);
            int64_t ts = static_cast<int64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());
            auto r = self->client_->sendEncryptedEvent(roomId, enc,
                "pd" + std::to_string(ts));
            LOG(LogChannel::GUI, "sendThreadReply enc: ok=%d http=%d",
                r.ok ? 1 : 0, r.httpStatus);
            QMetaObject::invokeMethod(guard, [guard, self, r, effectiveRoot, text, ts]() {
                if (guard.isNull() || self.isNull()) return;
                if (r.ok) {
                    DisplayedEvent echo;
                    echo.eventId = r.data;
                    echo.senderId = self->client_->account().userId;
                    if (!echo.senderId.empty() && echo.senderId[0] == '@') {
                        auto colon = echo.senderId.find(':');
                        echo.senderName = (colon != std::string::npos) ? echo.senderId.substr(1, colon - 1) : echo.senderId.substr(1);
                    }
                    echo.originServerTs = ts;
                    echo.type = "m.room.message";
                    echo.msgtype = "m.text";
                    echo.body = text;
                     echo.isThreadReply = true;
                     echo.threadRootId = effectiveRoot;
                     self->timelineModel_->appendBack(echo);
                 } else {
                    self->statusLabel_->setText("�� " + QString::fromStdString(r.error.message));
                }
            }, Qt::QueuedConnection);
        }
    });
}

} // namespace progressive::desktop
