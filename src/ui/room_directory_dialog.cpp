// src/ui/room_directory_dialog.cpp
#include "room_directory_dialog.hpp"

#include <QApplication>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QThread>
#include <QMetaObject>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <thread>

#include <progressive/json_parser.hpp>

namespace progressive::desktop {

RoomDirectoryDialog::RoomDirectoryDialog(MatrixClient* client, QWidget* parent)
    : QDialog(parent), client_(client) {
    setWindowTitle("Public Rooms — Browse & Join");
    setModal(true);
    resize(700, 500);

    searchEdit_ = new QLineEdit(this);
    searchEdit_->setPlaceholderText("Search public rooms (e.g. 'matrix', 'linux')...");
    serverEdit_ = new QLineEdit("matrix.org", this);
    serverEdit_->setPlaceholderText("homeserver (optional)");

    searchBtn_ = new QPushButton("Search", this);
    resultsList_ = new QListWidget(this);
    loadMoreBtn_ = new QPushButton("Load more...", this);
    loadMoreBtn_->setEnabled(false);
    statusLabel_ = new QLabel("Enter a search term and click Search.", this);
    closeBtn_ = new QPushButton("Close", this);

    auto* form = new QFormLayout;
    form->addRow("Search:", searchEdit_);
    form->addRow("Server:", serverEdit_);

    auto* btnRow = new QHBoxLayout;
    btnRow->addWidget(searchBtn_);
    btnRow->addStretch();
    btnRow->addWidget(closeBtn_);

    auto* root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addLayout(btnRow);
    root->addWidget(statusLabel_);
    root->addWidget(resultsList_);
    root->addWidget(loadMoreBtn_);

    connect(searchBtn_, &QPushButton::clicked, this, &RoomDirectoryDialog::onSearchClicked);
    connect(loadMoreBtn_, &QPushButton::clicked, this, &RoomDirectoryDialog::onLoadMoreClicked);
    connect(closeBtn_, &QPushButton::clicked, this, &QDialog::accept);
    connect(resultsList_, &QListWidget::itemDoubleClicked, this, &RoomDirectoryDialog::onJoinClicked);

    // Auto-search on Enter
    connect(searchEdit_, &QLineEdit::returnPressed, this, &RoomDirectoryDialog::onSearchClicked);
}

void RoomDirectoryDialog::doSearch(const std::string& query, const std::string& server, const std::string& from) {
    statusLabel_->setText("Searching...");
    QApplication::processEvents();

    // Run in worker thread
    std::thread([this, query, server, from]() {
        auto r = client_->searchPublicRooms(server, query, 20, from);

        QMetaObject::invokeMethod(this, [this, r, from]() {
            if (!r.ok) {
                statusLabel_->setText("Error: " + QString::fromStdString(r.error.message));
                return;
            }

            // Parse JSON response
            auto body = r.data;
            // Extract next_batch
            auto nbPos = body.find("\"next_batch\"");
            if (nbPos != std::string::npos) {
                auto colon = body.find(':', nbPos);
                auto q1 = body.find('"', colon + 1);
                auto q2 = body.find('"', q1 + 1);
                if (q1 != std::string::npos && q2 != std::string::npos) {
                    nextBatch_ = QString::fromStdString(body.substr(q1 + 1, q2 - q1 - 1));
                }
            } else {
                nextBatch_.clear();
            }
            loadMoreBtn_->setEnabled(!nextBatch_.isEmpty());

            // Parse chunk array — find "chunk":[
            auto chunkPos = body.find("\"chunk\"");
            if (chunkPos == std::string::npos) {
                statusLabel_->setText("No rooms found.");
                return;
            }
            auto arrStart = body.find('[', chunkPos);
            auto arrEnd = body.find(']', arrStart);
            if (arrStart == std::string::npos || arrEnd == std::string::npos) {
                statusLabel_->setText("Parse error.");
                return;
            }

            // Split by objects — find each { } pair
            if (from.empty()) resultsList_->clear();
            int added = 0;
            size_t pos = arrStart + 1;
            while (pos < arrEnd) {
                auto objStart = body.find('{', pos);
                if (objStart == std::string::npos || objStart >= arrEnd) break;
                int depth = 1;
                size_t objEnd = objStart + 1;
                while (objEnd < arrEnd && depth > 0) {
                    if (body[objEnd] == '{') depth++;
                    else if (body[objEnd] == '}') depth--;
                    objEnd++;
                }
                std::string obj = body.substr(objStart, objEnd - objStart);

                // Extract fields
                auto roomId = progressive::parseJsonStringValue(obj, "room_id");
                auto name = progressive::parseJsonStringValue(obj, "name");
                auto topic = progressive::parseJsonStringValue(obj, "topic");
                auto alias = progressive::parseJsonStringValue(obj, "canonical_alias");

                // Extract member count
                auto mcPos = obj.find("\"num_joined_members\"");
                int members = 0;
                if (mcPos != std::string::npos) {
                    auto colon = obj.find(':', mcPos);
                    while (colon < obj.size() && !std::isdigit(static_cast<unsigned char>(obj[colon]))) colon++;
                    members = std::atoi(obj.c_str() + colon);
                }

                QString display = QString("%1 (%2 members)")
                    .arg(QString::fromStdString(name.empty() ? alias.empty() ? roomId : alias : name))
                    .arg(members);
                if (!topic.empty()) {
                    QString topicStr = QString::fromStdString(topic);
                    if (topicStr.size() > 80) topicStr = topicStr.left(80) + "...";
                    display += "\n    " + topicStr;
                }

                auto* item = new QListWidgetItem(display);
                item->setData(Qt::UserRole, QString::fromStdString(roomId));
                item->setData(Qt::UserRole + 1, QString::fromStdString(alias.empty() ? roomId : alias));
                item->setData(Qt::UserRole + 2, QString::fromStdString(name.empty() ? (alias.empty() ? roomId : alias) : name));
                resultsList_->addItem(item);
                added++;
                pos = objEnd;
            }

            statusLabel_->setText(QString("Found %1 room(s). Double-click to join.").arg(added));
        }, Qt::QueuedConnection);
    }).detach();
}

void RoomDirectoryDialog::onSearchClicked() {
    auto query = searchEdit_->text().trimmed().toStdString();
    auto server = serverEdit_->text().trimmed().toStdString();
    nextBatch_.clear();
    resultsList_->clear();
    doSearch(query, server, "");
}

void RoomDirectoryDialog::onLoadMoreClicked() {
    auto query = searchEdit_->text().trimmed().toStdString();
    auto server = serverEdit_->text().trimmed().toStdString();
    doSearch(query, server, nextBatch_.toStdString());
}

void RoomDirectoryDialog::onJoinClicked(QListWidgetItem* item) {
    if (!item) return;
    QString roomId = item->data(Qt::UserRole).toString();
    QString roomAlias = item->data(Qt::UserRole + 1).toString();
    QString roomName = item->data(Qt::UserRole + 2).toString();

    statusLabel_->setText("Joining " + roomAlias + "...");
    QApplication::processEvents();

    std::thread([this, roomId, roomAlias, roomName]() {
        auto r = client_->joinRoom(roomId.toStdString());

        QMetaObject::invokeMethod(this, [this, r, roomAlias, roomName]() {
            if (r.ok) {
                joinedRoomId_ = QString::fromStdString(r.data);
                joinedRoomName_ = roomName;
                statusLabel_->setText("Joined: " + roomAlias);
                QMessageBox::information(this, "Joined",
                    QString("Successfully joined %1").arg(roomAlias));
                accept();
            } else {
                statusLabel_->setText("Failed: " + QString::fromStdString(r.error.message));
                QMessageBox::warning(this, "Join failed",
                    QString("Failed to join %1:\n%2")
                        .arg(roomAlias)
                        .arg(QString::fromStdString(r.error.message)));
            }
        }, Qt::QueuedConnection);
    }).detach();
}

} // namespace progressive::desktop
