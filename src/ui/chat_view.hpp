// src/ui/chat_view.hpp — message input, sending, file attach, emoji react.
#pragma once
#include <QWidget>
#include "core/matrix_client.hpp"
#include "timeline_model.hpp"
#include <functional>
#include <string>

namespace progressive::desktop {

class MessageEdit;
class SyncEngine;

class ChatView : public QWidget {
    Q_OBJECT
public:
    ChatView(MatrixClient* client, TimelineModel* model, MessageEdit* edit,
             SyncEngine* sync, QWidget* parent = nullptr);

    void setCurrentRoom(const std::string& roomId, const std::string& threadRoot = "",
                        bool isEncrypted = false);
    void clear();

signals:
    void slashCommandForward(const std::string& cmd, const std::string& args);

private:
    void doSend(const std::string& body);
    void doAttachFile(const QString& path);
    void doQuickReact(const QString& emoji);

    MatrixClient* client_;
    TimelineModel* model_;
    MessageEdit* edit_;
    SyncEngine* sync_;
    std::string roomId_;
    std::string threadRoot_;
    bool encrypted_ = false;
};

} // namespace progressive::desktop
