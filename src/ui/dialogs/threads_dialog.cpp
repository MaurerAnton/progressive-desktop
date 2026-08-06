// src/ui/threads_dialog.cpp
#include "threads_dialog.hpp"

#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMetaObject>
#include <QThread>
#include <QMessageBox>
#include <QDateTime>
#include "core/thread_pool.hpp"
#include "../timeline/timeline_model.hpp"

#include <simdjson.h>
#include <cstdio>

namespace progressive::desktop {

namespace {
inline constexpr int kThreadsW  = 640;
inline constexpr int kThreadsH  = 440;
inline constexpr int kMaxPreviews = 25;   // root events fetched for previews
inline constexpr int kPageLimit = 25;     // /threads page size
} // namespace

ThreadsDialog::ThreadsDialog(MatrixClient* client, const std::string& roomId,
                             TimelineModel* timelineModel,
                             std::function<void(const QString&)> onOpenThread,
                             QWidget* parent)
    : QDialog(parent), client_(client), roomId_(roomId),
      timelineModel_(timelineModel), onOpenThread_(std::move(onOpenThread)) {
    setWindowTitle(QString("Threads in this room"));
    setModal(true);
    resize(kThreadsW, kThreadsH);

    list_ = new QListWidget(this);
    statusLabel_ = new QLabel("Loading threads...", this);
    refreshBtn_ = new QPushButton("Refresh", this);
    moreBtn_ = new QPushButton("Load more", this);
    moreBtn_->setVisible(false);
    closeBtn_ = new QPushButton("Close", this);

    auto* btnRow = new QHBoxLayout;
    btnRow->addWidget(refreshBtn_);
    btnRow->addWidget(moreBtn_);
    btnRow->addStretch();
    btnRow->addWidget(closeBtn_);

    auto* root = new QVBoxLayout(this);
    root->addWidget(statusLabel_);
    root->addWidget(list_);
    root->addLayout(btnRow);

    connect(refreshBtn_, &QPushButton::clicked, this, &ThreadsDialog::onRefreshClicked);
    connect(moreBtn_, &QPushButton::clicked, this, &ThreadsDialog::onLoadMoreClicked);
    connect(closeBtn_, &QPushButton::clicked, this, &QDialog::accept);
    connect(list_, &QListWidget::itemClicked, this, &ThreadsDialog::onThreadClicked);
    connect(list_, &QListWidget::itemDoubleClicked, this, &ThreadsDialog::onThreadClicked);

    loadThreads();
}

void ThreadsDialog::loadThreads() {
    statusLabel_->setText("Loading threads...");
    if (roomId_.empty()) {
        statusLabel_->setText("No room selected — open a room first.");
        return;
    }

    ThreadPool::instance().enqueue([this, roomId = roomId_, from = nextBatch_]() {
        auto r = client_->getThreads(roomId, from, kPageLimit);

        QMetaObject::invokeMethod(this, [this, r, roomId]() {
            if (!r.ok) {
                statusLabel_->setText("Error: " + QString::fromStdString(r.error.message));
                return;
            }

            simdjson::dom::parser parser;
            auto rootResult = parser.parse(r.data);
            if (rootResult.error() != simdjson::SUCCESS) {
                statusLabel_->setText("Failed to parse threads response.");
                return;
            }
            auto chunkResult = rootResult.value()["chunk"].get_array();
            if (chunkResult.error() != simdjson::SUCCESS) {
                statusLabel_->setText("No threads found.");
                return;
            }

            struct Entry { QString eventId; int replies = 0; bool participated = false; };
            std::vector<Entry> entries;
            for (auto entry : chunkResult.value()) {
                auto eid = entry["event_id"].get_string();
                if (eid.error() != simdjson::SUCCESS) continue;
                Entry e;
                e.eventId = QString::fromStdString(std::string(eid.value()));
                auto cnt = entry["count"].get_int64();
                if (cnt.error() == simdjson::SUCCESS) e.replies = static_cast<int>(cnt.value());
                auto part = entry["current_user_participated"].get_bool();
                if (part.error() == simdjson::SUCCESS) e.participated = part.value();
                entries.push_back(std::move(e));
            }
            auto nb = rootResult.value()["next_batch"].get_string();
            nextBatch_ = (nb.error() == simdjson::SUCCESS)
                ? std::string(nb.value()) : std::string();
            moreBtn_->setVisible(!nextBatch_.empty());
            int total = static_cast<int>(entries.size());
            if (total == 0) {
                if (list_->count() == 0)
                    statusLabel_->setText("No threads found.");
                else
                    statusLabel_->setText("No more threads.");
                return;
            }
            statusLabel_->setText(QString("Found %1 thread(s). Click one to open it.")
                                      .arg(list_->count() + total));

            int toFetch = qMin(total, kMaxPreviews);
            ThreadPool::instance().enqueue([this, entries = std::move(entries), toFetch, roomId]() {
                struct Preview { QString rowText; QString eventId; bool havePreview = false; };
                std::vector<Preview> previews;
                previews.reserve(toFetch);
                for (int i = 0; i < toFetch; ++i) {
                    const auto& e = entries[i];
                    Preview p;
                    p.eventId = e.eventId;
                    QString sender, body;
                    qint64 ts = 0;

                    // Prefer the LOCAL timeline: in encrypted rooms the root
                    // event is m.room.encrypted on the server, but we already
                    // hold the decrypted copy locally.
                    int row = timelineModel_ ? timelineModel_->findRow(e.eventId.toStdString()) : -1;
                    if (row >= 0) {
                        auto* ev = timelineModel_->at(row);
                        if (ev) {
                            sender = QString::fromStdString(ev->senderName);
                            body = QString::fromStdString(ev->body).left(80);
                            ts = ev->originServerTs;
                        }
                    }
                    if (sender.isEmpty()) {
                        auto ev = client_->getEvent(roomId, e.eventId.toStdString());
                        if (ev.ok) {
                            simdjson::dom::parser p2;
                            auto doc = p2.parse(ev.data);
                            if (doc.error() == simdjson::SUCCESS) {
                                auto s = doc.value()["sender"].get_string();
                                if (s.error() == simdjson::SUCCESS)
                                    sender = QString::fromStdString(std::string(s.value()));
                                auto t = doc.value()["origin_server_ts"].get_int64();
                                if (t.error() == simdjson::SUCCESS) ts = t.value();
                                auto bodyVal = doc.value()["content"]["body"].get_string();
                                if (bodyVal.error() == simdjson::SUCCESS)
                                    body = QString::fromStdString(std::string(bodyVal.value())).left(80);
                            }
                        }
                    }
                    if (!sender.isEmpty() || !body.isEmpty()) p.havePreview = true;
                    if (sender.isEmpty()) sender = "unknown";
                    if (body.isEmpty()) body = "(no preview)";
                    QString timeStr;
                    if (ts > 0)
                        timeStr = QDateTime::fromMSecsSinceEpoch(ts).toString("MMM d, HH:mm");
                    QString replyStr = e.replies > 0
                        ? QString(" · %1 repl%2").arg(e.replies).arg(e.replies == 1 ? "y" : "ies")
                        : QString();
                    QString partStr = e.participated ? QString(" · You") : QString();
                    p.rowText = QString("%1: %2%3%4%5")
                        .arg(sender.left(20), body,
                             replyStr.isEmpty() ? "" : replyStr,
                             partStr.isEmpty() ? "" : partStr,
                             timeStr.isEmpty() ? "" : QString(" · ") + timeStr);
                    previews.push_back(std::move(p));
                }
                QMetaObject::invokeMethod(this, [this, previews = std::move(previews)]() {
                    for (const auto& p : previews) {
                        auto* item = new QListWidgetItem(p.rowText);
                        item->setData(Qt::UserRole, p.eventId);
                        if (!p.havePreview)
                            item->setForeground(QColor("#888888"));
                        list_->addItem(item);
                    }
                }, Qt::QueuedConnection);
            });
        }, Qt::QueuedConnection);
    });
}

void ThreadsDialog::onRefreshClicked() {
    nextBatch_.clear();
    list_->clear();
    loadThreads();
}

void ThreadsDialog::onLoadMoreClicked() {
    loadThreads();
}

void ThreadsDialog::onThreadClicked(QListWidgetItem* item) {
    if (!item) return;
    QString eventId = item->data(Qt::UserRole).toString();
    if (eventId.isEmpty()) return;
    accept();
    if (onOpenThread_) onOpenThread_(eventId);
}

} // namespace progressive::desktop
