// src/ui/timeline_handlers.cpp — timeline context menu actions.
// Extracted from MainWindow::showTimelineContextMenu.

#include "timeline_handlers.hpp"
#include "core/matrix_client.hpp"
#include "timeline_model.hpp"
#include "emoji_picker.hpp"

#include <QMenu>
#include <QMessageBox>
#include <QInputDialog>
#include <QClipboard>
#include <QGuiApplication>
#include <QLabel>
#include <QPointer>
#include <thread>
#include <cstdio>

namespace progressive::desktop {

void handleReaction(QPointer<QWidget> parent, MatrixClient* client,
                     const std::string& roomId, const std::string& eventId,
                     TimelineModel* model, QLabel* statusLabel) {
    EmojiPicker picker(parent);
    QPointer<QWidget> guard = parent;
    QObject::connect(&picker, &EmojiPicker::emojiSelected, parent,
        [guard, client, roomId, eventId, model, statusLabel](const QString& emoji) {
            std::string em = emoji.toStdString();
            std::thread([guard, client, roomId, eventId = eventId, em, model, statusLabel]() {
                auto r = client->sendReaction(roomId, eventId, em);
                QMetaObject::invokeMethod(guard, [guard, r, eventId, em, model, statusLabel, client]() {
                    if (guard.isNull()) return;
                    if (r.ok) {
                        model->addReaction(eventId, em, client->account().userId, r.data);
                        if (statusLabel) statusLabel->setText("Reaction sent.");
                    }
                }, Qt::QueuedConnection);
            }).detach();
        });
    picker.exec();
}

void handleEdit(QPointer<QWidget> parent, MatrixClient* client,
                 const std::string& roomId, const std::string& eventId,
                 TimelineModel* model, QLabel* statusLabel) {
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
    std::thread([guard, client, roomId, eventId, newBody, model, statusLabel]() {
        auto r = client->editMessage(roomId, eventId, newBody);
        QMetaObject::invokeMethod(guard, [guard, r, eventId, newBody, model, statusLabel]() {
            if (guard.isNull()) return;
            if (r.ok) {
                model->updateBody(eventId, newBody);
                if (statusLabel) statusLabel->setText("Message edited.");
            }
        }, Qt::QueuedConnection);
    }).detach();
}

void handleDelete(QPointer<QWidget> parent, MatrixClient* client,
                   const std::string& roomId, const std::string& eventId,
                   TimelineModel* model, QLabel* statusLabel) {
    auto reply = QMessageBox::question(parent, "Delete",
        "Delete this message? This cannot be undone.",
        QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes) return;
    QPointer<QWidget> guard(parent);
    std::thread([guard, client, roomId, eventId, model, statusLabel]() {
        auto r = client->redactEvent(roomId, eventId);
        QMetaObject::invokeMethod(guard, [guard, r, eventId, model, statusLabel]() {
            if (guard.isNull()) return;
            if (r.ok) {
                model->markDeleted(eventId);
                if (statusLabel) statusLabel->setText("Message deleted.");
            }
        }, Qt::QueuedConnection);
    }).detach();
}

void handlePin(QPointer<QWidget> parent, MatrixClient* client,
                const std::string& roomId, const std::string& eventId,
                TimelineModel* model, QLabel* statusLabel) {
    QPointer<QWidget> guard(parent);
    std::thread([guard, client, roomId, eventId, model, statusLabel]() {
        auto r = client->pinMessage(roomId, eventId);
        QMetaObject::invokeMethod(guard, [guard, r, eventId, model, statusLabel]() {
            if (guard.isNull()) return;
            if (r.ok) {
                model->setPinned(eventId, true);
                if (statusLabel) statusLabel->setText("Message pinned.");
            }
        }, Qt::QueuedConnection);
    }).detach();
}

void handleCopyLink(QPointer<QWidget> parent, const std::string& roomId,
                     const std::string& eventId, QLabel* statusLabel) {
    (void)parent;
    std::string permalink = "https://matrix.to/#/" + roomId + "/" + eventId;
    QGuiApplication::clipboard()->setText(QString::fromStdString(permalink));
    if (statusLabel) statusLabel->setText("Permalink copied.");
}

} // namespace progressive::desktop
