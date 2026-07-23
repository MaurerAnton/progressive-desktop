// src/ui/handlers/room_context_menu.cpp — room context menu management.
#include "room_context_menu.hpp"
#include "thread_handler.hpp"
#include "core/matrix_client.hpp"
#include "core/thread_pool.hpp"
#include "../timeline/timeline_model.hpp"
#include "../timeline/timeline_handlers.hpp"
#include "../room_list_model.hpp"
#include "../main_window.hpp"

#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QMenu>
#include <QCursor>
#include <QPointer>
#include <QListView>

namespace progressive::desktop {

RoomContextMenu::RoomContextMenu(MatrixClient* client, TimelineModel* timelineModel,
                                   RoomListModel* roomModel, QListView* roomList,
                                   ThreadHandler* threadHandler, QLabel* statusLabel,
                                   QPointer<MainWindow> mw, QObject* parent)
    : QObject(parent), client_(client), timelineModel_(timelineModel),
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
    if (r->isInvite) {
        leaveAction->setVisible(false);
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
                    emit self->roomLeft(rid);
                } else {
                    self->statusLabel_->setText("Failed to leave: " + QString::fromStdString(res.error.message));
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
                } else {
                    self->statusLabel_->setText("Failed to reject: " + QString::fromStdString(res.error.message));
                }
            }, Qt::QueuedConnection);
        });
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
    bool hasReactions = false;
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
    bool canViewThread = false;
    QString threadRootForView;
    {
        int r = timelineModel_->findRow(eventId.toStdString());
        if (r >= 0) {
            auto* evt = timelineModel_->at(r);
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

    std::string roomIdStr = roomId;
    std::string eidStrVal = eventId.toStdString();

    if (selected == reactAction) {
        handleReaction(mw_.data(), client_, roomIdStr, eidStrVal, timelineModel_, statusLabel_);
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
        threadHandler_->replyInThread(eventId, roomIdStr);
    } else if (selected == viewThreadAction) {
        QString target = threadRootForView.isEmpty() ? eventId : threadRootForView;
        threadHandler_->openThreadView(target, roomIdStr);
    } else if (selected == copyLinkAction) {
        handleCopyLink(mw_.data(), roomIdStr, eidStrVal, statusLabel_);
    } else if (selected == editAction) {
        handleEdit(mw_.data(), client_, roomIdStr, eidStrVal, timelineModel_, statusLabel_);
    } else if (selected == deleteAction) {
        handleDelete(mw_.data(), client_, roomIdStr, eidStrVal, timelineModel_, statusLabel_);
    }
}

} // namespace progressive::desktop
