// src/ui/timeline_handlers.cpp — timeline context menu actions.
// Extracted from MainWindow::showTimelineContextMenu.

#include "timeline_handlers.hpp"
#include "core/matrix_client.hpp"
#include "core/crypto/decryptor.hpp"
#include "../handlers/room_key_helper.hpp"
#include "timeline_model.hpp"
#include "../chat/emoji_picker.hpp"

#include <QMenu>
#include <QMessageBox>
#include <QInputDialog>
#include <QClipboard>
#include <QGuiApplication>
#include <QLabel>
#include <QPointer>
#include "core/thread_pool.hpp"
#include <cstdio>

namespace progressive::desktop {

void handleReaction(QPointer<QWidget> parent, const std::shared_ptr<MatrixClient>& client,
                     const std::string& roomId, const std::string& eventId,
                     TimelineModel* model, QLabel* statusLabel,
                     bool encrypted, Decryptor* dec) {
    EmojiPicker picker(parent);
    QPointer<QWidget> guard = parent;
    QObject::connect(&picker, &EmojiPicker::emojiSelected, parent,
        [guard, client, roomId, eventId, model, statusLabel, encrypted, dec](const QString& emoji) {
            std::string em = emoji.toStdString();
            ThreadPool::instance().enqueue([guard, client, roomId, eventId = eventId, em, model, statusLabel, encrypted, dec]() {
                ApiResult<std::string> r;
                if (encrypted && dec && dec->isInitialized()) {
                    std::string sessId = dec->getOrCreateOutboundSession(roomId);
                    std::string content = "{\"m.relates_to\":{\"rel_type\":\"m.annotation\",\"event_id\":\""
                                          + eventId + "\",\"key\":\"" + em + "\"}}";
                    if (!sessId.empty()) {
                        std::string inner = "{\"type\":\"m.reaction\",\"content\":" + content +
                                            ",\"room_id\":\"" + roomId + "\"}";
                        std::string enc = dec->encryptMessage(roomId, client->account().deviceId, inner);
                        if (!enc.empty()) {
                            if (!dec->roomKeyShared(roomId)) shareRoomKeyForRoom(*client, *dec, roomId);
                            r = client->sendEncryptedEvent(roomId, enc, genTxnId("enc"));
                        }
                    }
                    if (!r.ok && r.error.message.empty())
                        r.error.message = "reaction encryption failed";
                } else {
                    r = client->sendReaction(roomId, eventId, em);
                }
                QMetaObject::invokeMethod(guard, [guard, r, eventId, em, model, statusLabel, client]() {
                    if (guard.isNull()) return;
                    if (r.ok) {
                        model->addReaction(eventId, em, client->account().userId, r.data);
                        if (statusLabel) statusLabel->setText("Reaction sent.");
                    }
                }, Qt::QueuedConnection);
            });
        });
    picker.exec();
}

void handleEdit(QPointer<QWidget> parent, const std::shared_ptr<MatrixClient>& client,
                 const std::string& roomId, const std::string& eventId,
                 TimelineModel* model, QLabel* statusLabel,
                 bool encrypted, Decryptor* dec) {
    int row = model->findRow(eventId);
    if (row < 0) return;
    auto* evt = model->at(row);
    if (!evt) return;
    bool ok;
    QString newText = QInputDialog::getText(parent, "Edit message",
        "New text:", QLineEdit::Normal, QString::fromStdString(evt->body), &ok);
    if (!ok || newText.trimmed().isEmpty()) return;
    std::string newBody = newText.toStdString();
    QPointer<QWidget> guard(parent);
    ThreadPool::instance().enqueue([guard, client, roomId, eventId, newBody, model, statusLabel, encrypted, dec]() {
        ApiResult<std::string> r;
        if (encrypted && dec && dec->isInitialized()) {
            // Edits leak plaintext if sent raw in E2EE rooms — megolm-wrap
            // the m.replace event like any other message.
            std::string escaped;
            for (char ch : newBody) {
                if (ch == '"') escaped += "\\\"";
                else if (ch == '\\') escaped += "\\\\";
                else if (ch == '\n') escaped += "\\n";
                else escaped += ch;
            }
            std::string sessId = dec->getOrCreateOutboundSession(roomId);
            std::string inner = "{\"type\":\"m.room.message\",\"content\":{\"msgtype\":\"m.text\",\"body\":\"" +
                                escaped + "\",\"m.new_content\":{\"msgtype\":\"m.text\",\"body\":\"" + escaped +
                                "\"},\"m.relates_to\":{\"rel_type\":\"m.replace\",\"event_id\":\"" + eventId +
                                "\"}},\"room_id\":\"" + roomId + "\"}";
            if (!sessId.empty()) {
                std::string enc = dec->encryptMessage(roomId, client->account().deviceId, inner);
                if (!enc.empty()) {
                    if (!dec->roomKeyShared(roomId)) shareRoomKeyForRoom(*client, *dec, roomId);
                    r = client->sendEncryptedEvent(roomId, enc, genTxnId("edit"));
                }
            }
            if (!r.ok && r.error.message.empty())
                r.error.message = "edit encryption failed";
        } else {
            r = client->editMessage(roomId, eventId, newBody);
        }
        QMetaObject::invokeMethod(guard, [guard, r, eventId, newBody, model, statusLabel]() {
            if (guard.isNull()) return;
            if (r.ok) {
                model->updateBody(eventId, newBody);
                if (statusLabel) statusLabel->setText("Message edited.");
            }
        }, Qt::QueuedConnection);
    });
}

void handleDelete(QPointer<QWidget> parent, const std::shared_ptr<MatrixClient>& client,
                   const std::string& roomId, const std::string& eventId,
                   TimelineModel* model, QLabel* statusLabel) {
    auto reply = QMessageBox::question(parent, "Delete",
        "Delete this message? This cannot be undone.",
        QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes) return;
    QPointer<QWidget> guard(parent);
    ThreadPool::instance().enqueue([guard, client, roomId, eventId, model, statusLabel]() {
        auto r = client->redactEvent(roomId, eventId);
        QMetaObject::invokeMethod(guard, [guard, r, eventId, model, statusLabel]() {
            if (guard.isNull()) return;
            if (r.ok) {
                model->markDeleted(eventId);
                if (statusLabel) statusLabel->setText("Message deleted.");
            }
        }, Qt::QueuedConnection);
    });
}

void handlePin(QPointer<QWidget> parent, const std::shared_ptr<MatrixClient>& client,
                const std::string& roomId, const std::string& eventId,
                TimelineModel* model, QLabel* statusLabel) {
    QPointer<QWidget> guard(parent);
    ThreadPool::instance().enqueue([guard, client, roomId, eventId, model, statusLabel]() {
        auto r = client->pinMessage(roomId, eventId);
        QMetaObject::invokeMethod(guard, [guard, r, eventId, model, statusLabel]() {
            if (guard.isNull()) return;
            if (r.ok) {
                model->setPinned(eventId, true);
                if (statusLabel) statusLabel->setText("Message pinned.");
            }
        }, Qt::QueuedConnection);
    });
}

void handleCopyLink(QPointer<QWidget> parent, const std::string& roomId,
                     const std::string& eventId, QLabel* statusLabel) {
    (void)parent;
    std::string permalink = "https://matrix.to/#/" + roomId + "/" + eventId;
    QGuiApplication::clipboard()->setText(QString::fromStdString(permalink));
    if (statusLabel) statusLabel->setText("Permalink copied.");
}

} // namespace progressive::desktop
