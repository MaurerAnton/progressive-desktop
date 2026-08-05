// src/ui/chat_view.cpp — message sending logic extracted from MainWindow.
#include "chat_view.hpp"
#include "chat_logger.hpp"
#include "message_edit.hpp"
#include "core/sync_engine.hpp"
#include "core/debug_log.hpp"
#include "core/memory_stats.hpp"
#include "../room_list_model.hpp"
#include "../handlers/room_key_helper.hpp"

#include <QFileDialog>
#include <QPointer>
#include <QDateTime>
#include <QMimeDatabase>
#include <QFileInfo>
#include <QDir>
#include <QTimer>

#include "core/thread_pool.hpp"
#include <chrono>
#include <ctime>
#include <sstream>
#include <simdjson.h>

namespace progressive::desktop {

ChatView::ChatView(std::shared_ptr<MatrixClient> client, TimelineModel* model, MessageEdit* edit,
                   SyncEngine* sync, QWidget* parent)
    : QWidget(parent), client_(std::move(client)), model_(model), edit_(edit), sync_(sync) {
    connect(edit_, &MessageEdit::sendMessage, this, [this](const std::string& body) {
        std::fprintf(stderr, "[chat] send: room=%s body=\"%s\" encrypted=%d thread=%s\n",
                     roomId_.c_str(), body.c_str(), encrypted_ ? 1 : 0,
                     threadRoot_.empty() ? "(none)" : threadRoot_.c_str());
        doSend(body);
    });
    connect(edit_, &MessageEdit::slashCommand, this, [this](const std::string& cmd, const std::string& args) {
        if (cmd == "me" && !roomId_.empty()) {
            std::fprintf(stderr, "[chat] slash: /me %s\n", args.c_str());
            DisplayedEvent echo;
            echo.senderId = client_->account().userId;
            echo.senderName = "you";
            echo.type = "m.room.message";
            echo.msgtype = "m.emote";
            echo.body = args;
            echo.originServerTs = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
            model_->appendBack(echo);
            // DEBT(E2EE): /me emote bypasses encryption — never checks room state
            ThreadPool::instance().enqueue([client = client_, roomId = roomId_, body = args]() {
                client->sendMessage(roomId, body, "m.emote");
            });
        } else {
            emit slashCommandForward(cmd, args);
        }
    });
    connect(edit_, &MessageEdit::attachFileRequested, this, [this]() {
        std::fprintf(stderr, "[chat] file: room=%s attach requested\n", roomId_.c_str());
        if (roomId_.empty() || !client_) return;
        QString filePath = QFileDialog::getOpenFileName(this, "Attach file",
            QString(), "All files (*.*)");
        if (!filePath.isEmpty()) {
            std::fprintf(stderr, "[chat] file: selected path=%s\n", filePath.toUtf8().data());
            doAttachFile(filePath);
        }
    });
    connect(edit_, &MessageEdit::quickReact, this, [this](const QString& emoji) {
        std::fprintf(stderr, "[chat] react: room=%s emoji=%s\n",
                     roomId_.c_str(), emoji.toUtf8().data());
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
    if (roomId_.empty() || !client_) {
        std::fprintf(stderr, "[send] SKIP roomId_empty=%d client_null=%d\n",
                     roomId_.empty() ? 1 : 0, client_ ? 0 : 1);
        return;
    }
    std::fprintf(stderr, "[send] message: room=%s body=\"%s\"\n", roomId_.c_str(), body.c_str());
    DisplayedEvent echo;
    echo.eventId = "pending-" + std::to_string(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    echo.senderId = client_->account().userId;
    echo.senderName = "you";
    echo.type = "m.room.message";
    echo.msgtype = "m.text";
    echo.body = body;
    echo.originServerTs = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    if (!threadRoot_.empty()) { echo.isThreadReply = true; echo.threadRootId = threadRoot_; }
    std::string tempId = echo.eventId;
    model_->appendBack(echo);
    if (chatLogger_ && chatLogger_->active()) chatLogger_->log(body);

    std::string roomId = roomId_;
    std::string threadRoot = threadRoot_;
    auto client = client_;
    QString myUserId = QString::fromStdString(client->account().userId);
    QPointer<ChatView> guard(this);

    bool encrypted = encrypted_;
    if (!encrypted && roomListModel_) {
        int row = roomListModel_->findRowByRoomId(roomId);
        if (row >= 0) {
            auto* rd = roomListModel_->at(row);
            if (rd) encrypted = rd->isEncrypted;
        }
    }
    auto sendStartNs = std::chrono::steady_clock::now();
    ThreadPool::instance().enqueue([guard, client, roomId, body, tempId, myUserId, threadRoot, encrypted, sendStartNs]() {
        auto enqNs = std::chrono::steady_clock::now();
        auto enqMs = std::chrono::duration_cast<std::chrono::milliseconds>(enqNs - sendStartNs).count();
        // Thread reply (unencrypted)
        if (!threadRoot.empty() && !encrypted) {
            auto t0 = std::chrono::steady_clock::now();
            auto r = client->sendThreadReply(roomId, body, threadRoot);
            LOG(LogChannel::GUI, "send: enqueueDelayMs=%lld httpElapsedMs=%lld ok=%d", (long long)enqMs,
                (long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count(),
                r.ok ? 1 : 0);
            if (!r.ok) {
                std::fprintf(stderr, "[send] FAILED thread: %s (code=%s)\n",
                             r.error.message.c_str(), r.error.code.c_str());
                QMetaObject::invokeMethod(guard, [guard, r, tempId]() {
                    if (guard.isNull()) return;
                    guard->model_->replaceEcho(tempId, {.msgtype = "m.notice", .body = "❌ " + r.error.message});
                }, Qt::QueuedConnection);
                return;
            }
            QMetaObject::invokeMethod(guard, [guard, r, tempId, body, myUserId, threadRoot]() {
                if (guard.isNull()) return;
                DisplayedEvent real;
                real.eventId = r.data; real.senderId = myUserId.toStdString();
                real.senderName = "you"; real.type = "m.room.message";
                real.msgtype = "m.text"; real.body = body;
                real.isThreadReply = true; real.threadRootId = threadRoot;
                real.originServerTs = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
                guard->model_->replaceEcho(tempId, real);
            }, Qt::QueuedConnection);
            return;
        }
        // Encrypted (Megolm)
        if (encrypted && guard && guard->sync_ && guard->sync_->decryptor()->isInitialized()) {
            auto* dec = guard->sync_->decryptor();
            std::string deviceId = client->account().deviceId;
            std::string sessId = dec->getOrCreateOutboundSession(roomId);
            LOG(LogChannel::E2EE, "doSend: encrypted room=%.30s sessId=%.20s sessIdEmpty=%d",
                roomId.c_str(), sessId.c_str(), sessId.empty() ? 1 : 0);
            if (sessId.empty()) {
                std::fprintf(stderr, "[send] FAILED: outbound session creation\n");
                QMetaObject::invokeMethod(guard, [guard, tempId]() {
                    if (guard.isNull()) return;
                    guard->model_->replaceEcho(tempId, {.msgtype = "m.notice", .body = "❌ Failed to encrypt"});
                }, Qt::QueuedConnection);
                return;
            }
            std::string escaped;
            for (char c : body) {
                if (c == '"') escaped += "\\\""; else if (c == '\\') escaped += "\\\\";
                else if (c == '\n') escaped += "\\n"; else escaped += c;
            }
            std::string relatesTo;
            if (!threadRoot.empty()) {
                relatesTo = ",\"m.relates_to\":{\"rel_type\":\"m.thread\",\"event_id\":\""
                          + threadRoot + "\"}";
            }
            std::string inner = "{\"type\":\"m.room.message\",\"content\":{\"msgtype\":\"m.text\",\"body\":\""
                              + escaped + "\"" + relatesTo + "},\"room_id\":\""
                              + roomId + "\"}";
            std::string enc = dec->encryptMessage(roomId, deviceId, inner);
            LOG(LogChannel::E2EE, "doSend: enc size=%zu empty=%d", enc.size(), enc.empty() ? 1 : 0);
            if (enc.empty()) {
                std::fprintf(stderr, "[send] FAILED: encryption\n");
                QMetaObject::invokeMethod(guard, [guard, tempId]() {
                    if (guard.isNull()) return;
                    guard->model_->replaceEcho(tempId, {.msgtype = "m.notice", .body = "❌ Encryption failed"});
                }, Qt::QueuedConnection);
                return;
            }
            // Share room key ONCE per session — BEFORE sending the encrypted message
            // so the recipient's /sync processes the to-device room_key before the room event,
            // and the message decrypts immediately instead of showing 'Unable to decrypt'.
            if (!shareRoomKeyForRoom(*client, *dec, roomId)) {
                LOG(LogChannel::E2EE, "doSend: room key NOT shared (will retry next send) room=%.30s", roomId.c_str());
            }
            auto r = client->sendEncryptedEvent(roomId, enc, "pd" + std::to_string(std::time(nullptr)));
            if (!r.ok) {
                std::fprintf(stderr, "[send] FAILED encrypted: %s\n", r.error.message.c_str());
                QMetaObject::invokeMethod(guard, [guard, r, tempId]() {
                    if (guard.isNull()) return;
                    guard->model_->replaceEcho(tempId, {.msgtype = "m.notice", .body = "❌ " + r.error.message});
                }, Qt::QueuedConnection);
                return;
            }
            QMetaObject::invokeMethod(guard, [guard, r, tempId, body, myUserId]() {
                if (guard.isNull() || !r.ok) return;
                DisplayedEvent real;
                real.eventId = r.data; real.senderId = myUserId.toStdString();
                real.senderName = "you"; real.type = "m.room.message";
                real.msgtype = "m.text"; real.body = body;
                real.originServerTs = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
                guard->model_->replaceEcho(tempId, real);
            }, Qt::QueuedConnection);
        } else {
            auto r = client->sendMessage(roomId, body);
            if (!r.ok) {
                std::fprintf(stderr, "[send] FAILED message: %s (code=%s)\n",
                             r.error.message.c_str(), r.error.code.c_str());
                QMetaObject::invokeMethod(guard, [guard, r, tempId]() {
                    if (guard.isNull()) return;
                    guard->model_->replaceEcho(tempId, {.msgtype = "m.notice", .body = "❌ " + r.error.message});
                }, Qt::QueuedConnection);
                return;
            }
            QMetaObject::invokeMethod(guard, [guard, r, tempId, body, myUserId]() {
                if (guard.isNull() || !r.ok) return;
                DisplayedEvent real;
                real.eventId = r.data; real.senderId = myUserId.toStdString();
                real.senderName = "you"; real.type = "m.room.message";
                real.msgtype = "m.text"; real.body = body;
                real.originServerTs = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
                guard->model_->replaceEcho(tempId, real);
            });
        }
    });
}

void ChatView::doAttachFile(const QString& filePath) {
    if (roomId_.empty() || !client_) return;
    std::fprintf(stderr, "[send] file: room=%s path=%s\n",
                 roomId_.c_str(), filePath.toUtf8().data());
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
    auto client = client_;
    bool encrypted = encrypted_;
    QPointer<ChatView> guard(this);

    ThreadPool::instance().enqueue([guard, client, roomId, fn, ct, filePath, isImage, isVideo, isAudio, encrypted]() {
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
        // Encrypted room: wrap the media event in m.room.encrypted (Megolm),
        // mirroring the text path. The media bytes stay on the media repo;
        // the mxc URL + filename now travel inside the encrypted event.
        ApiResult<std::string> r;
        if (encrypted && guard && guard->sync_ && guard->sync_->decryptor()->isInitialized()) {
            auto* dec = guard->sync_->decryptor();
            std::string sessId = dec->getOrCreateOutboundSession(roomId);
            if (sessId.empty()) {
                QMetaObject::invokeMethod(guard, [guard]() {
                    if (guard.isNull()) return;
                    guard->model_->appendBack({.msgtype = "m.notice", .body = "❌ Failed to encrypt media"});
                }, Qt::QueuedConnection);
                return;
            }
            std::string inner = "{\"type\":\"m.room.message\",\"content\":" + body.str() +
                                ",\"room_id\":\"" + roomId + "\"}";
            std::string enc = dec->encryptMessage(roomId, client->account().deviceId, inner);
            if (enc.empty()) {
                QMetaObject::invokeMethod(guard, [guard]() {
                    if (guard.isNull()) return;
                    guard->model_->appendBack({.msgtype = "m.notice", .body = "❌ Encryption failed"});
                }, Qt::QueuedConnection);
                return;
            }
            shareRoomKeyForRoom(*client, *dec, roomId);
            r = client->sendEncryptedEvent(roomId, enc, "pd" + std::to_string(std::time(nullptr)));
        } else {
            r = client->sendMessageEvent(roomId, "m.room.message", body.str());
        }
        if (!r.ok) std::fprintf(stderr, "[send] FAILED file: %s\n", r.error.message.c_str());
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
            echo.originServerTs = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
            guard->model_->appendBack(echo);
        }, Qt::QueuedConnection);
    });
}

void ChatView::doQuickReact(const QString& emoji) {
    if (roomId_.empty() || !client_) return;
    int lastRow = model_->rowCount(QModelIndex()) - 1;
    if (lastRow < 0) return;
    auto* evt = model_->at(lastRow);
    if (!evt || evt->eventId.empty()) return;
    std::fprintf(stderr, "[send] react: room=%s event=%s emoji=%s\n",
                 roomId_.c_str(), evt->eventId.c_str(), emoji.toUtf8().data());
    std::string eid = evt->eventId;
    std::string em = emoji.toStdString();
    auto client = client_;
    std::string roomId = roomId_;
    bool encrypted = encrypted_;
    QPointer<ChatView> guard(this);
    ThreadPool::instance().enqueue([guard, client, roomId, eid, em, encrypted]() {
        ApiResult<std::string> r;
        if (encrypted && guard && guard->sync_ && guard->sync_->decryptor()->isInitialized()) {
            auto* dec = guard->sync_->decryptor();
            std::string sessId = dec->getOrCreateOutboundSession(roomId);
            std::string content = "{\"m.relates_to\":{\"rel_type\":\"m.annotation\",\"event_id\":\""
                                  + eid + "\",\"key\":\"" + em + "\"}}";
            if (!sessId.empty()) {
                std::string inner = "{\"type\":\"m.reaction\",\"content\":" + content +
                                    ",\"room_id\":\"" + roomId + "\"}";
                std::string enc = dec->encryptMessage(roomId, client->account().deviceId, inner);
                if (!enc.empty())
                    r = client->sendEncryptedEvent(roomId, enc, "pd" + std::to_string(std::time(nullptr)));
            }
            if (!r.ok && r.error.message.empty())
                r.error.message = "reaction encryption failed";
        } else {
            r = client->sendReaction(roomId, eid, em);
        }
        QMetaObject::invokeMethod(guard, [guard, r, eid, em]() {
            if (guard.isNull() || !r.ok) return;
            guard->model_->addReaction(eid, em, guard->client_->account().userId, r.data);
        }, Qt::QueuedConnection);
    });
}

void ChatView::onImagePasted(const QImage& image) {
    if (roomId_.empty() || !client_) return;
    QString tempPath = QDir::tempPath() + "/progressive_paste_" +
        QString::number(QDateTime::currentMSecsSinceEpoch()) + ".png";
    if (!image.save(tempPath, "PNG")) return;
    doAttachFile(tempPath);
    // DEBT(UI): temp file cleanup — left in Qt temp dir, auto-cleaned on exit
}

} // namespace progressive::desktop
