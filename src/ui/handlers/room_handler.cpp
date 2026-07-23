// src/ui/room_handler.cpp — room interaction extracted from MainWindow.
#include "room_handler.hpp"
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

std::string jsonUnescape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\') { out += s[i]; continue; }
        if (++i >= s.size()) break;
        char c = s[i];
        switch (c) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'u': {
                if (i + 4 >= s.size()) { out += 'u'; break; }
                unsigned cp = 0;
                for (int j = 1; j <= 4; ++j) {
                    char hex = s[i + j];
                    cp <<= 4;
                    if (hex >= '0' && hex <= '9') cp |= (hex - '0');
                    else if (hex >= 'a' && hex <= 'f') cp |= (hex - 'a' + 10);
                    else if (hex >= 'A' && hex <= 'F') cp |= (hex - 'A' + 10);
                    else { cp = 0; break; }
                }
                i += 4;
                if (cp < 0x80) out += static_cast<char>(cp);
                else if (cp < 0x800) {
                    out += static_cast<char>(0xC0 | (cp >> 6));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                } else {
                    out += static_cast<char>(0xE0 | (cp >> 12));
                    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                }
                break;
            }
            default: out += '\\'; out += c; break;
        }
    }
    return out;
}

std::string extractJsonStringDecoded(std::string_view json, std::string_view key) {
    std::string pat1 = std::string("\"") + std::string(key) + "\":\"";
    auto pos = json.find(pat1);
    if (pos != std::string_view::npos) {
        pos += pat1.size();
        auto end = pos;
        while (end < json.size()) {
            if (json[end] == '\\' && end + 1 < json.size()) { end += 2; continue; }
            if (json[end] == '"') break;
            ++end;
        }
        if (end < json.size()) return jsonUnescape(json.substr(pos, end - pos));
    }
    std::string pat2 = std::string("\"") + std::string(key) + "\": \"";
    pos = json.find(pat2);
    if (pos != std::string_view::npos) {
        pos += pat2.size();
        auto end = pos;
        while (end < json.size()) {
            if (json[end] == '\\' && end + 1 < json.size()) { end += 2; continue; }
            if (json[end] == '"') break;
            ++end;
        }
        if (end < json.size()) return jsonUnescape(json.substr(pos, end - pos));
    }
    return {};
}

std::string extractMxcUrl(std::string_view contentJson) {
    return extractJsonStringDecoded(contentJson, "url");
}

std::string extractMimetype(std::string_view contentJson) {
    return extractJsonStringDecoded(contentJson, "mimetype");
}

std::string extractBody(std::string_view contentJson) {
    return extractJsonStringDecoded(contentJson, "body");
}

std::string extractMsgtype(std::string_view contentJson) {
    return extractJsonStringDecoded(contentJson, "msgtype");
}

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

void RoomHandler::onRoomClicked(const QModelIndex& idx) {
    const RoomData* r = roomModel_->at(idx.row());
    if (!r) return;
    if (mainWindow_.isNull()) return;

    currentRoomIdStr_ = r->roomId;
    currentThreadRoot_.clear();
    mainWindow_->threadBanner()->hide();
    currentPrevBatch_.clear();
    chatLogging_ = false;
    chatLogBtn_->setChecked(false);
    chatLogFile_.reset();
    mainWindow_->chatView()->setCurrentRoom(r->roomId, currentThreadRoot_, r->isEncrypted);
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
                MatrixClient* client = client_;
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
    QPointer<MainWindow> guard(mainWindow_);
    roomStore_->loadMembers(r->roomId, QPointer<QWidget>(guard.data()), senderIds,
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
        QPointer<QWidget>(guard.data()), [this](int count, const std::string& prevBatch) {
            if (count > 0) {
                currentPrevBatch_ = prevBatch;
                if (loadMoreBtn_ && !prevBatch.empty()) loadMoreBtn_->show();
                timelineView_->scrollToBottom();
                statusLabel_->setText(QString("Loaded %1 messages.").arg(count));
            }
        });
}

void RoomHandler::onRoomListContextMenu(const QPoint& pos) {
    auto idx = roomList_->indexAt(pos);
    if (!idx.isValid()) return;
    const RoomData* r = roomModel_->at(idx.row());
    if (!r) return;
    if (mainWindow_.isNull()) return;

    QMenu menu(mainWindow_.data());
    auto* leaveAction = menu.addAction("Leave room");
    auto* acceptAction = menu.addAction("Accept invite");
    auto* rejectAction = menu.addAction("Reject invite");
    if (r->isInvite) {
        leaveAction->setVisible(false);
    } else {
        acceptAction->setVisible(false);
        rejectAction->setVisible(false);
    }

    auto* selected = menu.exec(roomList_->mapToGlobal(pos));
    if (!selected) return;

    if (selected == leaveAction) {
        auto reply = QMessageBox::question(mainWindow_.data(), "Leave room",
            QString("Leave '%1'?").arg(QString::fromStdString(r->name)),
            QMessageBox::Yes | QMessageBox::No);
        if (reply != QMessageBox::Yes) return;

        std::string roomId = r->roomId;
        MatrixClient* client = client_;
        QPointer<MainWindow> guard(mainWindow_);
        QPointer<RoomHandler> self(this);
        statusLabel_->setText("Leaving room...");
        ThreadPool::instance().enqueue([guard, self, client, roomId]() {
            auto res = client->leaveRoom(roomId);
            QMetaObject::invokeMethod(guard, [guard, self, res, roomId]() {
                if (guard.isNull() || self.isNull()) return;
                if (res.ok) {
                    self->statusLabel_->setText("Left room.");
                    self->roomModel_->removeRoom(roomId);
                    if (self->currentRoomIdStr_ == roomId) {
                        self->currentRoomIdStr_.clear();
                        self->timelineModel_->clear();
                        self->timelineView_->hide();
                        self->timelinePlaceholder_->show();
                        self->messageEdit_->hide();
                        self->loadMoreBtn_->parentWidget()->hide();
                        self->chatLogging_ = false;
                        self->chatLogFile_.reset();
                        self->chatLogBtn_->setChecked(false);
                        self->chatLogBtn_->setText(" Save");
                    }
                } else {
                    self->statusLabel_->setText("Failed to leave: " + QString::fromStdString(res.error.message));
                }
            }, Qt::QueuedConnection);
        });
    } else if (selected == acceptAction) {
        std::string roomId = r->roomId;
        MatrixClient* client = client_;
        QPointer<MainWindow> guard(mainWindow_);
        QPointer<RoomHandler> self(this);
        statusLabel_->setText("Joining room...");
        ThreadPool::instance().enqueue([guard, self, client, roomId]() {
            auto res = client->joinRoom(roomId);
            QMetaObject::invokeMethod(guard, [guard, self, res, roomId]() {
                if (guard.isNull() || self.isNull()) return;
                if (res.ok) {
                    self->statusLabel_->setText("Joined room.");
                    RoomData* rd = const_cast<RoomData*>(self->roomModel_->at(
                        self->roomModel_->findRowByRoomId(roomId)));
                    if (rd) {
                        rd->isInvite = false;
                        int row = self->roomModel_->findRowByRoomId(roomId);
                        if (row >= 0) {
                            QModelIndex idx = self->roomModel_->index(row);
                            emit self->roomModel_->dataChanged(idx, idx);
                        }
                    }
                } else {
                    self->statusLabel_->setText("Failed to join: " + QString::fromStdString(res.error.message));
                }
            }, Qt::QueuedConnection);
        });
    } else if (selected == rejectAction) {
        std::string roomId = r->roomId;
        MatrixClient* client = client_;
        QPointer<MainWindow> guard(mainWindow_);
        QPointer<RoomHandler> self(this);
        statusLabel_->setText("Rejecting invite...");
        ThreadPool::instance().enqueue([guard, self, client, roomId]() {
            auto res = client->leaveRoom(roomId);
            QMetaObject::invokeMethod(guard, [guard, self, res, roomId]() {
                if (guard.isNull() || self.isNull()) return;
                if (res.ok) {
                    self->statusLabel_->setText("Invite rejected.");
                    self->roomModel_->removeRoom(roomId);
                } else {
                    self->statusLabel_->setText("Failed to reject: " + QString::fromStdString(res.error.message));
                }
            }, Qt::QueuedConnection);
        });
    }
}

void RoomHandler::onLoadMoreClicked() {
    if (currentRoomIdStr_.empty() || !client_ || currentPrevBatch_.empty()) return;
    if (mainWindow_.isNull()) return;

    statusLabel_->setText("Loading older messages...");
    loadMoreBtn_->setEnabled(false);

    std::string roomId = currentRoomIdStr_;
    std::string from = currentPrevBatch_;
    MatrixClient* client = client_;
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
                    de.msgtype = extractMsgtype(de.contentJson);
                    de.body = extractBody(de.contentJson);
                    if (de.msgtype == "m.image" || de.msgtype == "m.video") {
                        de.mxcUrl = extractMxcUrl(de.contentJson);
                        de.mimetype = extractMimetype(de.contentJson);
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
    showTimelineContextMenu(eventId, globalPos);
}

void RoomHandler::showTimelineContextMenu(const QString& eventId, const QPoint& globalPos) {
    if (mainWindow_.isNull()) return;

    QMenu menu(mainWindow_.data());
    auto* reactAction = menu.addAction("Add reaction...");

    auto* removeReactMenu = menu.addMenu("Remove reaction");
    std::string eidStr = eventId.toStdString();
    int row = timelineModel_->findRow(eidStr);
    std::string myUserId = client_ ? client_->account().userId : "";
    bool hasReactions = false;
    if (row >= 0) {
        auto* evt = timelineModel_->at(row);
        if (evt) {
            for (const auto& r : evt->reactions) {
                for (const auto& uid : r.userIds) {
                    if (uid == myUserId) {
                        auto* action = removeReactMenu->addAction(QString::fromStdString(r.emoji));
                        connect(action, &QAction::triggered, this, [this, eventId, eidStr, r]() {
                            if (currentRoomIdStr_.empty() || !client_) return;
                            ThreadPool::instance().enqueue([this, roomId = currentRoomIdStr_, reid = r.reactionEventId, eidStr, emoji = r.emoji]() {
                                auto res = client_->redactEvent(roomId, reid);
                                QMetaObject::invokeMethod(this, [this, res, eidStr, emoji]() {
                                    if (res.ok) {
                                        timelineModel_->removeReaction(eidStr, emoji, client_->account().userId);
                                        statusLabel_->setText("Reaction removed.");
                                    }
                                }, Qt::QueuedConnection);
                            });
                        });
                        hasReactions = true;
                    }
                }
            }
        }
    }
    if (!hasReactions) removeReactMenu->setEnabled(false);

    menu.addSeparator();
    auto* pinAction = menu.addAction("Pin message");
    auto* unpinAction = menu.addAction("Unpin message");
    auto* replyThreadAction = menu.addAction("Reply in thread");
    auto* viewThreadAction = menu.addAction("View thread replies");
    bool canViewThread = false;
    QString threadRootForView;
    {
        int row = timelineModel_->findRow(eventId.toStdString());
        if (row >= 0) {
            auto* evt = timelineModel_->at(row);
            if (evt) {
                if (evt->threadReplyCount > 0) { canViewThread = true; threadRootForView = eventId; }
                else if (evt->isThreadReply && !evt->threadRootId.empty()) {
                    canViewThread = true; threadRootForView = QString::fromStdString(evt->threadRootId);
                }
            }
        }
    }
    if (!canViewThread) viewThreadAction->setEnabled(false);
    auto* copyLinkAction = menu.addAction("Copy permalink");
    menu.addSeparator();
    auto* editAction = menu.addAction("Edit");
    auto* deleteAction = menu.addAction("Delete");

    auto* selected = menu.exec(globalPos);
    if (!selected) return;

    std::string roomIdStr = currentRoomIdStr_;
    std::string eidStrVal = eventId.toStdString();

    if (selected == reactAction) {
        handleReaction(mainWindow_.data(), client_, roomIdStr, eidStrVal, timelineModel_, statusLabel_);
    } else if (selected == pinAction) {
        handlePin(mainWindow_.data(), client_, roomIdStr, eidStrVal, timelineModel_, statusLabel_);
    } else if (selected == unpinAction) {
        ThreadPool::instance().enqueue([this, roomIdStr, eidStrVal]() {
            auto r = client_->unpinMessage(roomIdStr, eidStrVal);
            QMetaObject::invokeMethod(this, [this, r, eidStrVal]() {
                if (r.ok) { timelineModel_->setPinned(eidStrVal, false); statusLabel_->setText("Message unpinned."); }
            }, Qt::QueuedConnection);
        });
    } else if (selected == replyThreadAction) {
        if (currentRoomIdStr_.empty() || !client_) return;
        QString rootText;
        int row = timelineModel_->findRow(eventId.toStdString());
        if (row >= 0) { auto* evt = timelineModel_->at(row); if (evt) rootText = QString::fromStdString(evt->body); }
        bool ok;
        QString reply = QInputDialog::getText(mainWindow_.data(), "Reply in thread",
            QString("Replying to:\n\"%1\"\n\nYour reply:").arg(rootText.left(100)), QLineEdit::Normal, "", &ok);
        if (!ok || reply.trimmed().isEmpty()) return;
        ThreadPool::instance().enqueue([this, roomIdStr, eidStrVal, body = reply.toStdString(), threadRoot = currentThreadRoot_]() {
            std::string effectiveRoot = threadRoot.empty() ? eidStrVal : threadRoot;
            auto r = client_->sendThreadReply(roomIdStr, body, effectiveRoot);
            QMetaObject::invokeMethod(this, [this, r, eidStrVal]() {
                if (r.ok) {
                    int rootRow = timelineModel_->findRow(eidStrVal);
                    if (rootRow >= 0) {
                        auto* rootEvt = timelineModel_->at(rootRow);
                        if (rootEvt) { rootEvt->threadReplyCount++; emit timelineModel_->dataChanged(timelineModel_->index(rootRow), timelineModel_->index(rootRow)); }
                    }
                }
            }, Qt::QueuedConnection);
        });
    } else if (selected == viewThreadAction) {
        openThreadView(threadRootForView.isEmpty() ? eventId : threadRootForView);
    } else if (selected == copyLinkAction) {
        handleCopyLink(mainWindow_.data(), roomIdStr, eidStrVal, statusLabel_);
    } else if (selected == editAction) {
        handleEdit(mainWindow_.data(), client_, roomIdStr, eidStrVal, timelineModel_, statusLabel_);
    } else if (selected == deleteAction) {
        handleDelete(mainWindow_.data(), client_, roomIdStr, eidStrVal, timelineModel_, statusLabel_);
    }
}

void RoomHandler::openThreadView(const QString& rootEventId) {
    if (!client_ || currentRoomIdStr_.empty()) return;
    if (mainWindow_.isNull()) return;

    currentThreadRoot_ = rootEventId.toStdString();
    timelineModel_->clear();
    mainWindow_->threadBanner()->show();
    statusLabel_->setText("Loading thread...");

    std::string roomId = currentRoomIdStr_;
    std::string rootEid = rootEventId.toStdString();
    MatrixClient* client = client_;
    QPointer<MainWindow> guard(mainWindow_);
    QPointer<RoomHandler> self(this);

    ThreadPool::instance().enqueue([guard, self, client, roomId, rootEid]() {
        auto r = client->getThreadReplies(roomId, rootEid);
        QMetaObject::invokeMethod(guard, [guard, self, r, rootEid]() {
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
            if (origResult.error() == simdjson::SUCCESS) {
                DisplayedEvent root;
                auto t = origResult.value()["type"].get_string();
                if (t.error() == simdjson::SUCCESS) root.type = std::string(t.value());
                auto eid = origResult.value()["event_id"].get_string();
                if (eid.error() == simdjson::SUCCESS) root.eventId = std::string(eid.value());
                auto sender = origResult.value()["sender"].get_string();
                if (sender.error() == simdjson::SUCCESS) root.senderId = std::string(sender.value());
                auto ts = origResult.value()["origin_server_ts"].get_int64();
                if (ts.error() == simdjson::SUCCESS) root.originServerTs = ts.value();
                auto contentResult = origResult.value()["content"];
                if (contentResult.error() == simdjson::SUCCESS) {
                    root.contentJson = simdjson::to_string(contentResult.value());
                }
                if (!root.senderId.empty() && root.senderId[0] == '@') {
                    auto colon = root.senderId.find(':');
                    if (colon != std::string::npos) root.senderName = root.senderId.substr(1, colon - 1);
                    else root.senderName = root.senderId.substr(1);
                }
                if (root.type == "m.room.message") {
                    root.msgtype = extractMsgtype(root.contentJson);
                    root.body = extractBody(root.contentJson);
                    if (root.msgtype == "m.image" || root.msgtype == "m.video") {
                        root.mxcUrl = extractMxcUrl(root.contentJson);
                        root.mimetype = extractMimetype(root.contentJson);
                    }
                }
                root.isThreadRoot = true;
                self->timelineModel_->appendBack(root);
            }

            auto chunkResult = rootResult.value()["chunk"].get_array();
            if (chunkResult.error() != simdjson::SUCCESS) {
                self->statusLabel_->setText("No thread replies found.");
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
                if (contentResult.error() == simdjson::SUCCESS) {
                    de.contentJson = simdjson::to_string(contentResult.value());
                }
                if (!de.senderId.empty() && de.senderId[0] == '@') {
                    auto colon = de.senderId.find(':');
                    if (colon != std::string::npos) de.senderName = de.senderId.substr(1, colon - 1);
                    else de.senderName = de.senderId.substr(1);
                }
                if (de.type == "m.room.message") {
                    de.msgtype = extractMsgtype(de.contentJson);
                    de.body = extractBody(de.contentJson);
                    if (de.msgtype == "m.image" || de.msgtype == "m.video") {
                        de.mxcUrl = extractMxcUrl(de.contentJson);
                        de.mimetype = extractMimetype(de.contentJson);
                    }
                }
                de.isThreadReply = true;
                events.push_back(std::move(de));
            }
            for (const auto& de : events) {
                self->timelineModel_->appendBack(de);
            }
            self->statusLabel_->setText(QString("Loaded %1 thread reply(s).").arg(events.size()));
        }, Qt::QueuedConnection);
    });
}

void RoomHandler::closeThreadView() {
    if (mainWindow_.isNull()) return;
    currentThreadRoot_.clear();
    mainWindow_->threadBanner()->hide();
    timelineModel_->clear();
    if (!currentRoomIdStr_.empty()) {
        QPointer<MainWindow> guard(mainWindow_);
        roomStore_->loadHistory(currentRoomIdStr_, timelineModel_,
            QPointer<QWidget>(guard.data()), [this](int count, const std::string& pb) {
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
    MatrixClient* client = client_;
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
    MatrixClient* client = client_;
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
