// src/ui/handlers/thread_handler.cpp — thread view management.
#include "thread_handler.hpp"
#include "core/matrix_client.hpp"
#include "core/thread_pool.hpp"
#include "../timeline/timeline_model.hpp"
#include "../room/room_store.hpp"
#include "../main_window.hpp"

#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QPointer>
#include <simdjson.h>

namespace progressive::desktop {

ThreadHandler::ThreadHandler(std::shared_ptr<MatrixClient> client, TimelineModel* timelineModel,
                               QLabel* threadBanner, QLabel* statusLabel,
                               QPointer<MainWindow> mw, QObject* parent)
    : QObject(parent), client_(std::move(client)), timelineModel_(timelineModel),
      threadBanner_(threadBanner), statusLabel_(statusLabel), mw_(mw) {}

void ThreadHandler::openThreadView(const QString& rootEventId, const std::string& roomId) {
    if (!client_ || roomId.empty()) return;
    if (mw_.isNull()) return;

    currentThreadRoot_ = rootEventId.toStdString();
    timelineModel_->clear();
    threadBanner_->show();
    statusLabel_->setText("Loading thread...");

    std::string rootEid = rootEventId.toStdString();
    QPointer<MainWindow> guard(mw_);
    QPointer<ThreadHandler> self(this);

    ThreadPool::instance().enqueue([guard, self, roomId, rootEid]() {
        auto r = self->client_->getThreadReplies(roomId, rootEid);
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
                if (contentResult.error() == simdjson::SUCCESS)
                    root.contentJson = simdjson::to_string(contentResult.value());
                if (!root.senderId.empty() && root.senderId[0] == '@') {
                    auto colon = root.senderId.find(':');
                    if (colon != std::string::npos) root.senderName = root.senderId.substr(1, colon - 1);
                    else root.senderName = root.senderId.substr(1);
                }
                if (root.type == "m.room.message") {
                    root.msgtype = msgType(root.contentJson);
                    root.body = msgBody(root.contentJson);
                    if (root.msgtype == "m.image" || root.msgtype == "m.video") {
                        root.mxcUrl = extractStringDec(root.contentJson, "url");
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
                if (contentResult.error() == simdjson::SUCCESS)
                    de.contentJson = simdjson::to_string(contentResult.value());
                if (!de.senderId.empty() && de.senderId[0] == '@') {
                    auto colon = de.senderId.find(':');
                    if (colon != std::string::npos) de.senderName = de.senderId.substr(1, colon - 1);
                    else de.senderName = de.senderId.substr(1);
                }
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
        QString("Replying to:\n\"%1\"\n\nYour reply:").arg(rootText.left(100)),
        QLineEdit::Normal, "", &ok);
    if (!ok || reply.trimmed().isEmpty()) return;
    sendThreadReply(roomId, currentThreadRoot_, eventId.toStdString(), reply.toStdString());
}

void ThreadHandler::sendThreadReply(const std::string& roomId,
                                      const std::string& threadRoot,
                                      const std::string& replyToEventId,
                                      const std::string& text) {
    std::string effectiveRoot = threadRoot.empty() ? replyToEventId : threadRoot;
    QPointer<MainWindow> guard(mw_);
    QPointer<ThreadHandler> self(this);
    ThreadPool::instance().enqueue([guard, self, roomId, effectiveRoot, text]() {
        auto r = self->client_->sendThreadReply(roomId, text, effectiveRoot);
        QMetaObject::invokeMethod(guard, [guard, self, r, effectiveRoot]() {
            if (guard.isNull() || self.isNull()) return;
            if (r.ok) {
                int rootRow = self->timelineModel_->findRow(effectiveRoot);
                if (rootRow >= 0) {
                    auto* rootEvt = self->timelineModel_->at(rootRow);
                    if (rootEvt) {
                        rootEvt->threadReplyCount++;
                        emit self->timelineModel_->dataChanged(
                            self->timelineModel_->index(rootRow),
                            self->timelineModel_->index(rootRow));
                    }
                }
            }
        }, Qt::QueuedConnection);
    });
}

} // namespace progressive::desktop
