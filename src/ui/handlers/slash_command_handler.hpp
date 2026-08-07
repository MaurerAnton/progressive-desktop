// src/ui/handlers/slash_command_handler.hpp
#pragma once
#include <QObject>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace progressive::desktop {

class MatrixClient;
class TimelineModel;
class AuthHandler;

// Element-style slash-command registry. Each command has a name, an args
// help string and a handler(client, roomId, args). Unknown commands produce
// a feedback row instead of being sent as messages.
class SlashCommandHandler : public QObject {
    Q_OBJECT
public:
    SlashCommandHandler(TimelineModel* timelineModel, AuthHandler* auth,
                        QObject* parent = nullptr);

    void setClient(std::shared_ptr<MatrixClient> c) { client_ = std::move(c); }
    void setRoomIdProvider(std::function<std::string()> fn) { roomIdProvider_ = std::move(fn); }

public slots:
    void handleCommand(const std::string& cmd, const std::string& args);

private:
    struct Command {
        std::string name;
        std::string argsHelp;
        std::string description;
        void (SlashCommandHandler::*run)(const std::string& roomId,
                                         const std::string& args);
    };

    void systemRow(const std::string& body);
    void runInvite(const std::string& roomId, const std::string& args);
    void runJoin(const std::string& roomId, const std::string& args);
    void runLeave(const std::string& roomId, const std::string& args);
    void runKick(const std::string& roomId, const std::string& args);
    void runBan(const std::string& roomId, const std::string& args);
    void runUnban(const std::string& roomId, const std::string& args);
    void runRoomname(const std::string& roomId, const std::string& args);
    void runClear(const std::string& roomId, const std::string& args);
    void runLogout(const std::string& roomId, const std::string& args);

    std::shared_ptr<MatrixClient> client_;
    std::function<std::string()> roomIdProvider_;
    TimelineModel* timelineModel_;
    AuthHandler* auth_;
    std::vector<Command> commands_;
};

} // namespace progressive::desktop
