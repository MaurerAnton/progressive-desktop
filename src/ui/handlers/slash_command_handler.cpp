#include "slash_command_handler.hpp"
#include "../timeline/timeline_model.hpp"
#include "auth_handler.hpp"

namespace progressive::desktop {

SlashCommandHandler::SlashCommandHandler(TimelineModel* timelineModel, AuthHandler* auth,
                                         QObject* parent)
    : QObject(parent), timelineModel_(timelineModel), auth_(auth) {}

void SlashCommandHandler::handleCommand(const std::string& cmd, const std::string& args) {
    (void)args;
    if (cmd == "help") {
        DisplayedEvent sys;
        sys.type = "m.room.message";
        sys.msgtype = "m.notice";
        sys.body = "Commands: /help /clear /logout /me <action>";
        sys.senderName = "system";
        timelineModel_->appendBack(sys);
    } else if (cmd == "clear") {
        timelineModel_->clear();
    } else if (cmd == "logout") {
        auth_->logout();
    }
}

} // namespace progressive::desktop
