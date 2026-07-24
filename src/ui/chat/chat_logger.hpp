// src/ui/chat/chat_logger.hpp — extracted chat logging for RoomHandler & ToolbarHandler.
#pragma once
#include <string>
#include <fstream>
#include <memory>

namespace progressive::desktop {

class ChatLogger {
public:
    void start(const std::string& roomId, const std::string& roomName);
    void stop();
    void log(const std::string& line);
    bool active() const { return file_ && file_->is_open(); }

private:
    std::unique_ptr<std::ofstream> file_;
    std::string roomId_;
};

} // namespace
