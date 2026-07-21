// src/ui/chat_view.hpp — message input, sending, file attach, emoji picker.
#pragma once
#include <QWidget>
#include "core/matrix_client.hpp"
#include "timeline_model.hpp"
#include <functional>
#include <string>

class QTextEdit;

namespace progressive::desktop {

class MessageEdit;

// ChatView encapsulates message sending logic extracted from MainWindow.
// Handles: local echo, encrypted send, thread reply, file attach, emoji react.
class ChatView : public QWidget {
    Q_OBJECT
public:
    ChatView(MatrixClient* client, TimelineModel* model, MessageEdit* edit,
             QWidget* parent = nullptr);

    void setCurrentRoom(const std::string& roomId, const std::string& threadRoot = "",
                        bool isEncrypted = false);
    void clear();
    bool currentRoomId() const { return roomId_; }

    using SendCallback = std::function<void()>;  // for UI updates after send
    void onSend(SendCallback cb) { sendCb_ = std::move(cb); }

private:
    void doSend(const std::string& body);
    void doAttachFile(const QString& path);
    void doQuickReact(const QString& emoji);

    MatrixClient* client_;
    TimelineModel* model_;
    MessageEdit* edit_;
    std::string roomId_;
    std::string threadRoot_;
    bool encrypted_ = false;
    SendCallback sendCb_;
};

} // namespace progressive::desktop
