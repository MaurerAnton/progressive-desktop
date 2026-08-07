#include "slash_command_handler.hpp"
#include "../timeline/timeline_model.hpp"
#include "auth_handler.hpp"
#include "core/matrix_client.hpp"
#include "core/thread_pool.hpp"
#include "core/debug_log.hpp"

#include <QMetaObject>
#include <QPointer>

namespace progressive::desktop {

SlashCommandHandler::SlashCommandHandler(TimelineModel* timelineModel, AuthHandler* auth,
                                         QObject* parent)
    : QObject(parent), timelineModel_(timelineModel), auth_(auth) {
    commands_ = {
        {"help", "", "List commands"},
        {"invite", "<user-id>", "Invite a user to this room"},
        {"join", "<room-id-or-alias>", "Join a room"},
        {"leave", "", "Leave this room"},
        {"kick", "<user-id> [reason]", "Kick a user from this room"},
        {"ban", "<user-id> [reason]", "Ban a user from this room"},
        {"unban", "<user-id>", "Unban a user in this room"},
        {"roomname", "<name>", "Rename this room"},
        {"clear", "", "Clear the timeline"},
        {"logout", "", "Log out"},
    };
}

void SlashCommandHandler::systemRow(const std::string& body) {
    DisplayedEvent sys;
    sys.type = "m.room.message";
    sys.msgtype = "m.notice";
    sys.body = body;
    sys.senderName = "system";
    timelineModel_->appendBack(sys);
}

void SlashCommandHandler::handleCommand(const std::string& cmd, const std::string& args) {
    if (cmd == "help") {
        std::string list = "Commands:";
        for (const auto& c : commands_) {
            list += "\n/" + c.name + (c.argsHelp.empty() ? "" : " " + c.argsHelp) +
                    " — " + c.description;
        }
        systemRow(list);
        return;
    }
    for (const auto& c : commands_) {
        if (c.name == cmd) {
            (this->*c.run)(
                roomIdProvider_ ? roomIdProvider_() : std::string(),
                args);
            return;
        }
    }
    systemRow("Unknown command: /" + cmd + " (try /help)");
}

void SlashCommandHandler::runInvite(const std::string& roomId, const std::string& args) {
    if (!client_ || roomId.empty()) { systemRow("Open a room first."); return; }
    auto space = args.find(' ');
    std::string userId = (space == std::string::npos) ? args : args.substr(0, space);
    std::string reason = (space == std::string::npos) ? "" : args.substr(space + 1);
    if (userId.empty()) { systemRow("Usage: /invite <user-id>"); return; }
    if (!userId.empty() && userId[0] != '@') userId = "@" + userId;
    auto client = client_;
    QPointer<SlashCommandHandler> self(this);
    ThreadPool::instance().enqueue([self, client, roomId, userId]() {
        auto r = client->inviteUser(roomId, userId);
        QMetaObject::invokeMethod(self, [self, r, userId]() {
            if (self.isNull()) return;
            self->systemRow(r.ok ? ("Invited " + userId)
                                 : ("❌ Invite failed: " + r.error.message));
        }, Qt::QueuedConnection);
    });
}

void SlashCommandHandler::runJoin(const std::string& roomId, const std::string& args) {
    if (!client_ || args.empty()) { systemRow("Usage: /join <room-id-or-alias>"); return; }
    auto client = client_;
    QPointer<SlashCommandHandler> self(this);
    ThreadPool::instance().enqueue([self, client, args]() {
        auto r = client->joinRoom(args);
        QMetaObject::invokeMethod(self, [self, r]() {
            if (self.isNull()) return;
            self->systemRow(r.ok ? "Joined the room."
                                 : ("❌ Join failed: " + r.error.message));
        }, Qt::QueuedConnection);
    });
}

void SlashCommandHandler::runLeave(const std::string& roomId, const std::string& args) {
    (void)args;
    if (!client_ || roomId.empty()) { systemRow("Open a room first."); return; }
    auto client = client_;
    QPointer<SlashCommandHandler> self(this);
    ThreadPool::instance().enqueue([self, client, roomId]() {
        auto r = client->leaveRoom(roomId);
        QMetaObject::invokeMethod(self, [self, r]() {
            if (self.isNull()) return;
            self->systemRow(r.ok ? "Left the room." : "❌ Leave failed.");
        }, Qt::QueuedConnection);
    });
}

void SlashCommandHandler::runKick(const std::string& roomId, const std::string& args) {
    if (!client_ || roomId.empty()) { systemRow("Open a room first."); return; }
    auto space = args.find(' ');
    std::string userId = (space == std::string::npos) ? args : args.substr(0, space);
    std::string reason = (space == std::string::npos) ? "" : args.substr(space + 1);
    if (userId.empty()) { systemRow("Usage: /kick <user-id> [reason]"); return; }
    auto client = client_;
    QPointer<SlashCommandHandler> self(this);
    ThreadPool::instance().enqueue([self, client, roomId, userId, reason]() {
        auto r = client->kickUser(roomId, userId, reason);
        QMetaObject::invokeMethod(self, [self, r, userId]() {
            if (self.isNull()) return;
            self->systemRow(r.ok ? ("Kicked " + userId)
                                 : ("❌ Kick failed: " + r.error.message));
        }, Qt::QueuedConnection);
    });
}

void SlashCommandHandler::runBan(const std::string& roomId, const std::string& args) {
    if (!client_ || roomId.empty()) { systemRow("Open a room first."); return; }
    auto space = args.find(' ');
    std::string userId = (space == std::string::npos) ? args : args.substr(0, space);
    std::string reason = (space == std::string::npos) ? "" : args.substr(space + 1);
    if (userId.empty()) { systemRow("Usage: /ban <user-id> [reason]"); return; }
    auto client = client_;
    QPointer<SlashCommandHandler> self(this);
    ThreadPool::instance().enqueue([self, client, roomId, userId, reason]() {
        auto r = client->banUser(roomId, userId, reason);
        QMetaObject::invokeMethod(self, [self, r, userId]() {
            if (self.isNull()) return;
            self->systemRow(r.ok ? ("Banned " + userId)
                                 : ("❌ Ban failed: " + r.error.message));
        }, Qt::QueuedConnection);
    });
}

void SlashCommandHandler::runUnban(const std::string& roomId, const std::string& args) {
    if (!client_ || roomId.empty()) { systemRow("Open a room first."); return; }
    if (args.empty()) { systemRow("Usage: /unban <user-id>"); return; }
    auto client = client_;
    QPointer<SlashCommandHandler> self(this);
    ThreadPool::instance().enqueue([self, client, roomId, args]() {
        auto r = client->unbanUser(roomId, args);
        QMetaObject::invokeMethod(self, [self, r]() {
            if (self.isNull()) return;
            self->systemRow(r.ok ? "Unbanned." : "❌ Unban failed.");
        }, Qt::QueuedConnection);
    });
}

void SlashCommandHandler::runRoomname(const std::string& roomId, const std::string& args) {
    if (!client_ || roomId.empty()) { systemRow("Open a room first."); return; }
    if (args.empty()) { systemRow("Usage: /roomname <name>"); return; }
    auto client = client_;
    QPointer<SlashCommandHandler> self(this);
    ThreadPool::instance().enqueue([self, client, roomId, args]() {
        auto r = client->setRoomName(roomId, args);
        QMetaObject::invokeMethod(self, [self, r]() {
            if (self.isNull()) return;
            self->systemRow(r.ok ? "Room renamed." : "❌ Rename failed.");
        }, Qt::QueuedConnection);
    });
}

void SlashCommandHandler::runClear(const std::string& roomId, const std::string& args) {
    (void)roomId; (void)args;
    timelineModel_->clear();
}

void SlashCommandHandler::runLogout(const std::string& roomId, const std::string& args) {
    (void)roomId; (void)args;
    if (auth_) auth_->logout();
}

} // namespace progressive::desktop
