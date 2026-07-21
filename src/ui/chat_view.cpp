// src/ui/chat_view.cpp — message sending logic extracted from MainWindow.
#include "chat_view.hpp"
#include "message_edit.hpp"
#include "core/sync_engine.hpp"
#include "core/memory_stats.hpp"

#include <QFileDialog>
#include <QPointer>
#include <QDateTime>
#include <QMimeDatabase>
#include <QFileInfo>
#include <QTimer>

#include <thread>
#include <ctime>
#include <sstream>
#include <simdjson.h>

namespace progressive::desktop {

ChatView::ChatView(MatrixClient* client, TimelineModel* model, MessageEdit* edit,
                   SyncEngine* sync, QWidget* parent)
    : QWidget(parent), client_(client), model_(model), edit_(edit), sync_(sync) {
    connect(edit_, &MessageEdit::sendMessage, this, [this](const std::string& body) {
        doSend(body);
    });
    connect(edit_, &MessageEdit::slashCommand, this, [this](const std::string& cmd, const std::string& args) {
        if (cmd == "me" && !roomId_.empty()) {
            DisplayedEvent echo;
            echo.senderId = client_->account().userId;
            echo.senderName = "you";
            echo.type = "m.room.message";
            echo.msgtype = "m.emote";
            echo.body = args;
            echo.originServerTs = static_cast<int64_t>(QDateTime::currentMSecsSinceEpoch());
            model_->appendBack(echo);
            std::thread([client = client_, roomId = roomId_, body = args]() {
                client->sendMessage(roomId, body, "m.emote");
            }).detach();
        }
    });
    connect(edit_, &MessageEdit::attachFileRequested, this, [this]() {
        if (roomId_.empty() || !client_) return;
        QString filePath = QFileDialog::getOpenFileName(this, "Attach file",
            QString(), "All files (*.*)");
        if (!filePath.isEmpty()) doAttachFile(filePath);
    });
    connect(edit_, &MessageEdit::quickReact, this, [this](const QString& emoji) {
        doQuickReact(emoji);
    });
}

void ChatView::setCurrentRoom(const std::string& roomId, const std::string& threadRoot,
                               bool isEncrypted) {
    roomId_ = roomId;
    threadRoot_ = threadRoot;
    encrypted_ = isEncrypted;
}

void ChatView::clear() {
    roomId_.clear();
    threadRoot_.clear();
    encrypted_ = false;
}

void ChatView::doSend(const std::string& body) {
    if (roomId_.empty() || !client_) return;
    DisplayedEvent echo;
    echo.eventId = "pending-" + std::to_string(QDateTime::currentMSecsSinceEpoch());
    echo.senderId = client_->account().userId;
    echo.senderName = "you";
    echo.type = "m.room.message";
    echo.msgtype = "m.text";
    echo.body = body;
    echo.originServerTs = static_cast<int64_t>(QDateTime::currentMSecsSinceEpoch());
    if (!threadRoot_.empty()) { echo.isThreadReply = true; echo.threadRootId = threadRoot_; }
    std::string tempId = echo.eventId;
    model_->appendBack(echo);

    std::string roomId = roomId_;
    std::string threadRoot = threadRoot_;
    MatrixClient* client = client_;
    QString myUserId = QString::fromStdString(client->account().userId);
    QPointer<ChatView> guard(this);

    std::thread([guard, client, roomId, body, tempId, myUserId, threadRoot, encrypted = encrypted_]() {
        // Thread reply (unencrypted)
        if (!threadRoot.empty() && !encrypted) {
            auto r = client->sendThreadReply(roomId, body, threadRoot);
            QMetaObject::invokeMethod(guard, [guard, r, tempId, body, myUserId, threadRoot]() {
                if (guard.isNull() || !r.ok) return;
                DisplayedEvent real;
                real.eventId = r.data; real.senderId = myUserId.toStdString();
                real.senderName = "you"; real.type = "m.room.message";
                real.msgtype = "m.text"; real.body = body;
                real.isThreadReply = true; real.threadRootId = threadRoot;
                real.originServerTs = static_cast<int64_t>(QDateTime::currentMSecsSinceEpoch());
                guard->model_->replaceEcho(tempId, real);
            }, Qt::QueuedConnection);
            return;
        }
        // Encrypted (Megolm)
        if (encrypted && guard && guard->sync_ && guard->sync_->decryptor()->isInitialized()) {
            auto* dec = guard->sync_->decryptor();
            std::string deviceId = client->account().deviceId;
            std::string sessId = dec->getOrCreateOutboundSession(roomId);
            if (sessId.empty()) return;
            std::string escaped;
            for (char c : body) {
                if (c == '"') escaped += "\\\""; else if (c == '\\') escaped += "\\\\";
                else if (c == '\n') escaped += "\\n"; else escaped += c;
            }
            std::string inner = "{\"type\":\"m.room.message\",\"content\":{\"msgtype\":\"m.text\",\"body\":\"" + escaped + "\"}}";
            std::string enc = dec->encryptMessage(roomId, deviceId, inner);
            if (enc.empty()) return;
            auto r = client->sendEncryptedEvent(roomId, enc, "pd" + std::to_string(std::time(nullptr)));
            QMetaObject::invokeMethod(guard, [guard, r, tempId, body, myUserId]() {
                if (guard.isNull() || !r.ok) return;
                DisplayedEvent real;
                real.eventId = r.data; real.senderId = myUserId.toStdString();
                real.senderName = "you"; real.type = "m.room.message";
                real.msgtype = "m.text"; real.body = body;
                real.originServerTs = static_cast<int64_t>(QDateTime::currentMSecsSinceEpoch());
                guard->model_->replaceEcho(tempId, real);
            }, Qt::QueuedConnection);
            // Share room key with members (once per outbound session)
            std::string ourUserId = client->account().userId;
            std::string ourDeviceId = client->account().deviceId;
            std::string homeserver = client->account().homeserverUrl;
            std::string token = client->account().accessToken;
            auto* d = dec;
            std::thread([client, roomId, ourUserId, ourDeviceId, homeserver, token, d]() {
                auto membersResp = client->getRoomMembers(roomId);
                if (!membersResp.ok || !d) return;
                std::vector<std::string> userIds;
                simdjson::dom::parser mp;
                auto doc = mp.parse(membersResp.data);
                if (doc.error() != simdjson::SUCCESS) return;
                auto chunk = doc.value()["chunk"].get_array();
                if (chunk.error() != simdjson::SUCCESS) return;
                for (auto evt : chunk.value()) {
                    auto mship = evt["content"]["membership"].get_string();
                    if (mship.error() != simdjson::SUCCESS ||
                        std::string(mship.value()) != "join") continue;
                    auto sk = evt["state_key"].get_string();
                    if (sk.error() == simdjson::SUCCESS)
                        userIds.push_back(std::string(sk.value()));
                }
                if (!userIds.empty())
                    d->shareRoomKey(roomId, userIds, ourUserId, ourDeviceId, homeserver, token);
            }).detach();
        } else {
            auto r = client->sendMessage(roomId, body);
            QMetaObject::invokeMethod(guard, [guard, r, tempId, body, myUserId]() {
                if (guard.isNull() || !r.ok) return;
                DisplayedEvent real;
                real.eventId = r.data; real.senderId = myUserId.toStdString();
                real.senderName = "you"; real.type = "m.room.message";
                real.msgtype = "m.text"; real.body = body;
                real.originServerTs = static_cast<int64_t>(QDateTime::currentMSecsSinceEpoch());
                guard->model_->replaceEcho(tempId, real);
            }, Qt::QueuedConnection);
        }
    }).detach();
}

void ChatView::doAttachFile(const QString& filePath) {
    if (roomId_.empty() || !client_) return;
    QFileInfo fi(filePath);
    QString fileName = fi.fileName();
    QMimeDatabase db;
    QString contentType = db.mimeTypeForFile(filePath).name();
    bool isImage = contentType.startsWith("image/");
    bool isVideo = contentType.startsWith("video/");
    bool isAudio = contentType.startsWith("audio/");

    std::string roomId = roomId_;
    std::string fn = fileName.toStdString();
    std::string ct = contentType.toStdString();
    MatrixClient* client = client_;
    QPointer<ChatView> guard(this);

    std::thread([guard, client, roomId, fn, ct, filePath, isImage, isVideo, isAudio]() {
        QFile f(filePath);
        if (!f.open(QIODevice::ReadOnly)) return;
        QByteArray data = f.readAll(); f.close();
        std::vector<uint8_t> bytes(data.begin(), data.end());
        auto upload = client->uploadMedia(bytes, fn, ct);
        if (!upload.ok) return;
        std::ostringstream body;
        if (isImage) body << R"({"msgtype":"m.image","body":")" << fn << R"(","url":")" << upload.data << R"("})";
        else if (isVideo) body << R"({"msgtype":"m.video","body":")" << fn << R"(","url":")" << upload.data << R"("})";
        else if (isAudio) body << R"({"msgtype":"m.audio","body":")" << fn << R"(","url":")" << upload.data << R"("})";
        else body << R"({"msgtype":"m.file","body":")" << fn << R"(","url":")" << upload.data << R"("})";
        auto r = client->sendMessageEvent(roomId, "m.room.message", body.str());
        QMetaObject::invokeMethod(guard, [guard, r, fn, isImage, isVideo, isAudio, mxc = upload.data]() {
            if (guard.isNull() || !r.ok) return;
            DisplayedEvent echo;
            echo.eventId = r.data; echo.senderId = guard->client_->account().userId;
            echo.senderName = "you"; echo.type = "m.room.message";
            if (isImage) { echo.msgtype = "m.image"; echo.mxcUrl = mxc; }
            else if (isVideo) { echo.msgtype = "m.video"; echo.mxcUrl = mxc; }
            else if (isAudio) { echo.msgtype = "m.audio"; echo.mxcUrl = mxc; }
            else { echo.msgtype = "m.file"; echo.mxcUrl = mxc; }
            echo.body = fn;
            echo.originServerTs = static_cast<int64_t>(QDateTime::currentMSecsSinceEpoch());
            guard->model_->appendBack(echo);
        }, Qt::QueuedConnection);
    }).detach();
}

void ChatView::doQuickReact(const QString& emoji) {
    if (roomId_.empty() || !client_) return;
    int lastRow = model_->rowCount(QModelIndex()) - 1;
    if (lastRow < 0) return;
    auto* evt = model_->at(lastRow);
    if (!evt || evt->eventId.empty()) return;
    std::string eid = evt->eventId;
    std::string em = emoji.toStdString();
    MatrixClient* client = client_;
    std::string roomId = roomId_;
    QPointer<ChatView> guard(this);
    std::thread([guard, client, roomId, eid, em]() {
        auto r = client->sendReaction(roomId, eid, em);
        QMetaObject::invokeMethod(guard, [guard, r, eid, em]() {
            if (guard.isNull() || !r.ok) return;
            guard->model_->addReaction(eid, em, guard->client_->account().userId, r.data);
        }, Qt::QueuedConnection);
    }).detach();
}

} // namespace progressive::desktop
