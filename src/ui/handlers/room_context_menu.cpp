// src/ui/handlers/room_context_menu.cpp — room context menu management.
#include "room_context_menu.hpp"
#include "thread_handler.hpp"
#include "core/debug_log.hpp"
#include "core/matrix_client.hpp"
#include "core/thread_pool.hpp"
#include "../timeline/timeline_model.hpp"
#include "../timeline/timeline_handlers.hpp"
#include "../room_list_model.hpp"
#include "../main_window.hpp"
#include "core/crypto/decryptor.hpp"
#include <QFileDialog>
#include <QFile>
#include <QGuiApplication>
#include <QClipboard>
#include <simdjson.h>

#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QMenu>
#include <QCursor>
#include <QPointer>
#include <QListView>

namespace progressive::desktop {

RoomContextMenu::RoomContextMenu(std::shared_ptr<MatrixClient> client, TimelineModel* timelineModel,
                                   RoomListModel* roomModel, QListView* roomList,
                                   ThreadHandler* threadHandler, QLabel* statusLabel,
                                   QPointer<MainWindow> mw, QObject* parent)
    : QObject(parent), client_(std::move(client)), timelineModel_(timelineModel),
      roomModel_(roomModel), roomList_(roomList),
      threadHandler_(threadHandler), statusLabel_(statusLabel), mw_(mw) {}

void RoomContextMenu::onRoomListContextMenu(const QPoint& pos, const std::string& roomId) {
    auto idx = roomList_->indexAt(pos);
    if (!idx.isValid()) return;
    const RoomData* r = roomModel_->at(idx.row());
    if (!r) return;
    if (mw_.isNull()) return;

    QMenu menu(mw_.data());
    auto* leaveAction = menu.addAction("Leave room");
    auto* acceptAction = menu.addAction("Accept invite");
    auto* rejectAction = menu.addAction("Reject invite");
    auto* forgetAction = menu.addAction("Forget room");
    auto* hideAction = menu.addAction("Hide from list");
    if (r->isInvite) {
        leaveAction->setVisible(false);
        forgetAction->setVisible(false);
    } else {
        acceptAction->setVisible(false);
        rejectAction->setVisible(false);
    }

    auto* selected = menu.exec(roomList_->mapToGlobal(pos));
    if (!selected) return;

    if (selected == leaveAction) {
        auto reply = QMessageBox::question(mw_.data(), "Leave room",
            QString("Leave '%1'?").arg(QString::fromStdString(r->name)),
            QMessageBox::Yes | QMessageBox::No);
        if (reply != QMessageBox::Yes) return;

        std::string rid = r->roomId;
        QPointer<MainWindow> guard(mw_);
        QPointer<RoomContextMenu> self(this);
        statusLabel_->setText("Leaving room...");
        ThreadPool::instance().enqueue([guard, self, rid]() {
            auto res = self->client_->leaveRoom(rid);
            QMetaObject::invokeMethod(guard, [guard, self, res, rid]() {
                if (guard.isNull() || self.isNull()) return;
                if (res.ok) {
                    self->statusLabel_->setText("Left room.");
                    self->roomModel_->removeRoom(rid);
                    self->roomModel_->refreshHeader();
                    emit self->roomLeft(rid);
                    if (self->client_) {
                        if (auto* ss = self->client_->sessionStore())
                            ss->removeHiddenRoom(rid);
                    }
                    self->roomModel_->clearHiddenRoom(rid);
                    auto client = self->client_;
                    ThreadPool::instance().enqueue([client, rid]() {
                        client->forgetRoom(rid);
                    });
                } else {
                    std::string err = res.error.message;
                    if (!res.error.code.empty()) err = "[" + res.error.code + "] " + err;
                    self->statusLabel_->setText("Failed to leave: " + QString::fromStdString(err));
                }
            }, Qt::QueuedConnection);
        });
    } else if (selected == acceptAction) {
        std::string rid = r->roomId;
        QPointer<MainWindow> guard(mw_);
        QPointer<RoomContextMenu> self(this);
        statusLabel_->setText("Joining room...");
        ThreadPool::instance().enqueue([guard, self, rid]() {
            auto res = self->client_->joinRoom(rid);
            QMetaObject::invokeMethod(guard, [guard, self, res, rid]() {
                if (guard.isNull() || self.isNull()) return;
                if (res.ok) {
                    self->statusLabel_->setText("Joined room.");
                    RoomData* rd = const_cast<RoomData*>(self->roomModel_->at(
                        self->roomModel_->findRowByRoomId(rid)));
                    if (rd) {
                        rd->isInvite = false;
                        int row = self->roomModel_->findRowByRoomId(rid);
                        if (row >= 0) emit self->roomModel_->dataChanged(
                            self->roomModel_->index(row), self->roomModel_->index(row));
                        self->roomModel_->refreshHeader();
                    }
                } else {
                    self->statusLabel_->setText("Failed to join: " + QString::fromStdString(res.error.message));
                }
            }, Qt::QueuedConnection);
        });
    } else if (selected == rejectAction) {
        std::string rid = r->roomId;
        QPointer<MainWindow> guard(mw_);
        QPointer<RoomContextMenu> self(this);
        statusLabel_->setText("Rejecting invite...");
        ThreadPool::instance().enqueue([guard, self, rid]() {
            auto res = self->client_->leaveRoom(rid);
            QMetaObject::invokeMethod(guard, [guard, self, res, rid]() {
                if (guard.isNull() || self.isNull()) return;
                if (res.ok) {
                    self->statusLabel_->setText("Invite rejected.");
                    self->roomModel_->removeRoom(rid);
                    self->roomModel_->refreshHeader();
                    if (self->client_) {
                        if (auto* ss = self->client_->sessionStore())
                            ss->removeHiddenRoom(rid);
                    }
                    self->roomModel_->clearHiddenRoom(rid);
                    auto client = self->client_;
                    ThreadPool::instance().enqueue([client, rid]() {
                        client->forgetRoom(rid);
                    });
                } else {
                    std::string err = res.error.message;
                    if (!res.error.code.empty()) err = "[" + res.error.code + "] " + err;
                    self->statusLabel_->setText("Failed to reject: " + QString::fromStdString(err));
                }
            }, Qt::QueuedConnection);
        });
    } else if (selected == forgetAction) {
        std::string rid = r->roomId;
        QPointer<MainWindow> guard(mw_);
        QPointer<RoomContextMenu> self(this);
        statusLabel_->setText("Leaving + forgetting room...");
        ThreadPool::instance().enqueue([guard, self, rid]() {
            // Chain: leave first, then forget (spec requires prior leave)
            auto lr = self->client_->leaveRoom(rid);
            LOG(LogChannel::NET, "forget flow leave: room=%s ok=%d http=%d",
                rid.c_str(), lr.ok ? 1 : 0, lr.httpStatus);
            if (!lr.ok) {
                QMetaObject::invokeMethod(guard, [guard, self, lr, rid]() {
                    if (guard.isNull() || self.isNull()) return;
                    std::string err = lr.error.message;
                    if (!lr.error.code.empty()) err = "[" + lr.error.code + "] " + err;
                    self->statusLabel_->setText("Leave failed: " + QString::fromStdString(err));
                }, Qt::QueuedConnection);
                return;
            }
            auto fr = self->client_->forgetRoom(rid);
            LOG(LogChannel::NET, "forget flow forget: room=%s ok=%d http=%d",
                rid.c_str(), fr.ok ? 1 : 0, fr.httpStatus);
            QMetaObject::invokeMethod(guard, [guard, self, fr, rid]() {
                if (guard.isNull() || self.isNull()) return;
                if (fr.ok) {
                    self->statusLabel_->setText("Room forgotten.");
                    if (self->client_) {
                        if (auto* ss = self->client_->sessionStore())
                            ss->removeHiddenRoom(rid);
                    }
                    self->roomModel_->clearHiddenRoom(rid);
                    self->roomModel_->removeRoom(rid);
                    self->roomModel_->refreshHeader();
                } else {
                    std::string err = fr.error.message;
                    if (!fr.error.code.empty()) err = "[" + fr.error.code + "] " + err;
                    self->statusLabel_->setText("Left but forget failed: " + QString::fromStdString(err)
                        + " (hidden from list)");
                    if (self->client_) {
                        if (auto* ss = self->client_->sessionStore())
                            ss->removeHiddenRoom(rid);
                    }
                    self->roomModel_->clearHiddenRoom(rid);
                    self->roomModel_->removeRoom(rid);
                    self->roomModel_->refreshHeader();
                }
            }, Qt::QueuedConnection);
        });
    } else if (selected == hideAction) {
        // Local-only hide — for rooms where server refuses leave/reject/forget.
        std::string rid = r->roomId;
        if (client_) {
            if (auto* ss = client_->sessionStore()) ss->saveHiddenRoom(rid);
        }
        roomModel_->addHiddenRoom(rid);
        roomModel_->removeRoom(rid);
        roomModel_->refreshHeader();
        statusLabel_->setText("Hidden from list (server state unchanged).");
        LOG(LogChannel::GUI, "hideFromList: room=%s (local only, no server call)",
            rid.c_str());
    }
}

void RoomContextMenu::showTimelineContextMenu(const QString& eventId,
                                                const QPoint& globalPos,
                                                const std::string& roomId) {
    if (mw_.isNull()) return;

    QMenu menu(mw_.data());
    auto* reactAction = menu.addAction("Add reaction...");

    auto* removeReactMenu = menu.addMenu("Remove reaction");
    std::string eidStr = eventId.toStdString();
    int row = timelineModel_->findRow(eidStr);
    std::string myUserId = client_ ? client_->account().userId : "";
    bool isOwnMessage = false;
    bool isPinned = false;
    bool isSystemEvent = false;
    bool canViewThread = false;
    bool hasReactions = false;
    QString threadRootForView;
    QString decryptError;
    QString msgtype;
    std::string mediaKey, mediaIv, mediaSha, mediaBody;
    if (row >= 0) {
        auto* evt = timelineModel_->at(row);
        if (evt) {
            isOwnMessage = (evt->senderId == myUserId);
            decryptError = QString::fromStdString(evt->decryptError);
            msgtype = QString::fromStdString(evt->msgtype);
            mediaKey = evt->mediaKey; mediaIv = evt->mediaIv; mediaSha = evt->mediaSha256;
            mediaBody = evt->body;
            isPinned = evt->isPinned;
            isSystemEvent = (evt->type == "progressive.system");
            if (evt->threadReplyCount > 0) { canViewThread = true; threadRootForView = eventId; }
            else if (evt->isThreadReply && !evt->threadRootId.empty()) {
                canViewThread = true; threadRootForView = QString::fromStdString(evt->threadRootId);
            }
        }
    }
    bool isEncryptedRoom = false;
    if (roomModel_) {
        int rr = roomModel_->findRowByRoomId(roomId);
        if (rr >= 0) {
            auto* rd = roomModel_->at(rr);
            if (rd) isEncryptedRoom = rd->isEncrypted;
        }
    }
    if (row >= 0) {
        auto* evt = timelineModel_->at(row);
        if (evt) {
            for (const auto& r : evt->reactions) {
                for (const auto& uid : r.userIds) {
                    if (uid == myUserId) {
                        auto* action = removeReactMenu->addAction(QString::fromStdString(r.emoji));
                        connect(action, &QAction::triggered, this, [this, eventId, eidStr, r, roomId]() {
                            if (roomId.empty() || !client_) return;
                            ThreadPool::instance().enqueue([this, roomId, reid = r.reactionEventId, eidStr, emoji = r.emoji]() {
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
    if (!canViewThread) viewThreadAction->setEnabled(false);
    if (isSystemEvent) replyThreadAction->setEnabled(false);
    if (!isPinned) unpinAction->setEnabled(false);
    if (isPinned) pinAction->setEnabled(false);
    auto* copyLinkAction = menu.addAction("Copy permalink");
    auto* whyEncryptedAction = decryptError.isEmpty()
        ? nullptr
        : menu.addAction("Why is this encrypted?");
    auto* askAgainAction = decryptError.isEmpty()
        ? nullptr
        : menu.addAction("Request the key again");
    bool isMediaRow = (msgtype == "m.image" || msgtype == "m.video" ||
                       msgtype == "m.audio" || msgtype == "m.file");
    auto* downloadAction = isMediaRow ? menu.addAction("Download…") : nullptr;
    bool hasText = !mediaBody.empty() && !isSystemEvent;
    auto* copyTextAction = hasText ? menu.addAction("Copy text") : nullptr;
    menu.addSeparator();
    auto* editAction = menu.addAction("Edit");
    auto* deleteAction = menu.addAction("Delete");
    if (!isOwnMessage) editAction->setEnabled(false);

    auto* selected = menu.exec(globalPos);
    if (!selected) return;

    std::string roomIdStr = roomId;
    std::string eidStrVal = eventId.toStdString();

    if (selected == copyTextAction && copyTextAction) {
        QGuiApplication::clipboard()->setText(QString::fromStdString(mediaBody));
    } else if (selected == downloadAction && downloadAction) {
        QString suggested = mediaBody.empty()
            ? QString("download") : QString::fromStdString(mediaBody);
        QString path = QFileDialog::getSaveFileName(
            mw_.data(), "Save as", suggested, "All files (*.*)");
        if (path.isEmpty()) return;
        int drov = timelineModel_->findRow(eidStrVal);
        auto* devt = drov >= 0 ? timelineModel_->at(drov) : nullptr;
        if (!devt || devt->mxcUrl.empty()) return;
        std::string mxc = devt->mxcUrl;
        std::string dk = devt->mediaKey, div = devt->mediaIv, dsh = devt->mediaSha256;
        QString dest = path;
        auto client = client_;
        QPointer<RoomContextMenu> self(this);
        ThreadPool::instance().enqueue([self, client, mxc, dk, div, dsh, dest]() {
            auto res = dk.empty()
                ? client->downloadMedia(mxc, 0, 0)
                : client->downloadMediaEncrypted(mxc, dk, div, dsh);
            QMetaObject::invokeMethod(self, [self, res, dest]() {
                if (self.isNull()) return;
                if (!res.ok || res.data.empty()) {
                    if (self->statusLabel_)
                        self->statusLabel_->setText("Download failed.");
                    return;
                }
                QFile f(dest);
                if (f.open(QIODevice::WriteOnly)) {
                    f.write(reinterpret_cast<const char*>(res.data.data()),
                            static_cast<qint64>(res.data.size()));
                    f.close();
                    if (self->statusLabel_)
                        self->statusLabel_->setText("Saved to " + dest);
                } else if (self->statusLabel_) {
                    self->statusLabel_->setText("Could not write " + dest);
                }
            }, Qt::QueuedConnection);
        });
    } else if (selected == askAgainAction && askAgainAction) {
        // Manual key re-request (Element's "Ask for keys"): re-send the
        // m.room_key_request NOW with a fresh request_id.
        int rrow = timelineModel_->findRow(eidStrVal);
        auto* evt2 = rrow >= 0 ? timelineModel_->at(rrow) : nullptr;
        if (evt2 && mw_) {
            std::string sid, sk, devId;
            simdjson::dom::parser p;
            auto doc = p.parse(evt2->contentJson);
            if (doc.error() == simdjson::SUCCESS) {
                auto a = doc.value()["session_id"].get_string();
                auto b = doc.value()["sender_key"].get_string();
                auto c = doc.value()["device_id"].get_string();
                if (a.error() == simdjson::SUCCESS) sid = std::string(a.value());
                if (b.error() == simdjson::SUCCESS) sk = std::string(b.value());
                if (c.error() == simdjson::SUCCESS) devId = std::string(c.value());
            }
            if (!sid.empty() && !sk.empty() && mw_->decryptor()) {
                mw_->decryptor()->reRequestKey(roomIdStr, evt2->senderId, sk, sid, devId);
                statusLabel_->setText(QString("Key request sent to %1")
                    .arg(QString::fromStdString(evt2->senderId)));
            }
        }
    } else if (selected == whyEncryptedAction && whyEncryptedAction) {
        QMessageBox::information(mw_.data(), "Why is this encrypted?",
            "This message could not be decrypted.\n\n" + decryptError +
            "\n\nA room-key request was sent to the sender. If it stays "
            "encrypted, the sender's client may be withholding the key "
            "(verified-device policy) — check the Log viewer (E2EE channel).");
    } else if (selected == reactAction) {
        bool enc = false;
        int rrow = roomModel_->findRowByRoomId(roomIdStr);
        if (rrow >= 0 && roomModel_->at(rrow)) enc = roomModel_->at(rrow)->isEncrypted;
        handleReaction(mw_.data(), client_, roomIdStr, eidStrVal, timelineModel_, statusLabel_,
                       enc, mw_->decryptor());
    } else if (selected == pinAction) {
        handlePin(mw_.data(), client_, roomIdStr, eidStrVal, timelineModel_, statusLabel_);
    } else if (selected == unpinAction) {
        ThreadPool::instance().enqueue([this, roomIdStr, eidStrVal]() {
            auto r = client_->unpinMessage(roomIdStr, eidStrVal);
            QMetaObject::invokeMethod(this, [this, r, eidStrVal]() {
                if (r.ok) { timelineModel_->setPinned(eidStrVal, false); statusLabel_->setText("Message unpinned."); }
            }, Qt::QueuedConnection);
        });
    } else if (selected == replyThreadAction) {
        QString target = threadRootForView.isEmpty() ? eventId : threadRootForView;
        threadHandler_->replyInThread(target, roomIdStr);
    } else if (selected == viewThreadAction) {
        QString target = threadRootForView.isEmpty() ? eventId : threadRootForView;
        threadHandler_->openThreadView(target, roomIdStr);
    } else if (selected == copyLinkAction) {
        handleCopyLink(mw_.data(), roomIdStr, eidStrVal, statusLabel_);
    } else if (selected == editAction) {
        bool enc = false;
        int erow = roomModel_->findRowByRoomId(roomIdStr);
        if (erow >= 0 && roomModel_->at(erow)) enc = roomModel_->at(erow)->isEncrypted;
        handleEdit(mw_.data(), client_, roomIdStr, eidStrVal, timelineModel_, statusLabel_,
                   enc, mw_->decryptor());
    } else if (selected == deleteAction) {
        handleDelete(mw_.data(), client_, roomIdStr, eidStrVal, timelineModel_, statusLabel_);
    }
}

} // namespace progressive::desktop
