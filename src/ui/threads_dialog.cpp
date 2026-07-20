// src/ui/threads_dialog.cpp
#include "threads_dialog.hpp"

#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMetaObject>
#include <QThread>
#include <QMessageBox>
#include <thread>

#include <progressive/json_parser.hpp>

namespace progressive::desktop {

ThreadsDialog::ThreadsDialog(MatrixClient* client, const std::string& roomId, QWidget* parent)
    : QDialog(parent), client_(client), roomId_(roomId) {
    setWindowTitle("Threads in this room");
    setModal(true);
    resize(600, 400);

    list_ = new QListWidget(this);
    statusLabel_ = new QLabel("Loading threads...", this);
    refreshBtn_ = new QPushButton("Refresh", this);
    closeBtn_ = new QPushButton("Close", this);

    auto* btnRow = new QHBoxLayout;
    btnRow->addWidget(refreshBtn_);
    btnRow->addStretch();
    btnRow->addWidget(closeBtn_);

    auto* root = new QVBoxLayout(this);
    root->addWidget(statusLabel_);
    root->addWidget(list_);
    root->addLayout(btnRow);

    connect(refreshBtn_, &QPushButton::clicked, this, &ThreadsDialog::onRefreshClicked);
    connect(closeBtn_, &QPushButton::clicked, this, &QDialog::accept);
    connect(list_, &QListWidget::itemDoubleClicked, this, &ThreadsDialog::onThreadDoubleClicked);

    loadThreads();
}

void ThreadsDialog::loadThreads() {
    statusLabel_->setText("Loading threads...");
    list_->clear();

    std::thread([this]() {
        auto r = client_->getThreads(roomId_);

        QMetaObject::invokeMethod(this, [this, r]() {
            if (!r.ok) {
                statusLabel_->setText("Error: " + QString::fromStdString(r.error.message));
                return;
            }
            // Parse thread_root_ids array
            auto pos = r.data.find("\"thread_root_ids\"");
            if (pos == std::string::npos) {
                statusLabel_->setText("No threads found.");
                return;
            }
            auto arrStart = r.data.find('[', pos);
            auto arrEnd = r.data.find(']', arrStart);
            if (arrStart == std::string::npos || arrEnd == std::string::npos) {
                statusLabel_->setText("No threads found.");
                return;
            }

            int count = 0;
            size_t p = arrStart + 1;
            while (p < arrEnd) {
                auto q1 = r.data.find('"', p);
                if (q1 == std::string::npos || q1 >= arrEnd) break;
                auto q2 = r.data.find('"', q1 + 1);
                if (q2 == std::string::npos) break;
                auto eventId = r.data.substr(q1 + 1, q2 - q1 - 1);

                auto* item = new QListWidgetItem(QString("Thread: %1").arg(
                    QString::fromStdString(eventId).left(20) + "..."));
                item->setData(Qt::UserRole, QString::fromStdString(eventId));
                list_->addItem(item);
                count++;
                p = q2 + 1;
            }

            statusLabel_->setText(QString("Found %1 thread(s). Double-click to view replies.").arg(count));
        }, Qt::QueuedConnection);
    }).detach();
}

void ThreadsDialog::onRefreshClicked() {
    loadThreads();
}

void ThreadsDialog::onThreadDoubleClicked(QListWidgetItem* item) {
    if (!item) return;
    QString eventId = item->data(Qt::UserRole).toString();
    statusLabel_->setText("Loading replies for " + eventId.left(20) + "...");
    QApplication::processEvents();

    std::thread([this, eventId]() {
        auto r = client_->getThreadReplies(roomId_, eventId.toStdString());

        QMetaObject::invokeMethod(this, [this, r, eventId]() {
            if (!r.ok) {
                QMessageBox::warning(this, "Error", "Failed to load thread replies.");
                return;
            }
            // Show the raw JSON in a simple text view (full thread UI in next iteration)
            QMessageBox::information(this, "Thread replies",
                QString("Thread %1 has %2 bytes of replies.\n\n"
                        "Full thread timeline view will be added in the next update.")
                    .arg(eventId.left(20))
                    .arg(r.data.size()));
        }, Qt::QueuedConnection);
    }).detach();
}

} // namespace progressive::desktop
