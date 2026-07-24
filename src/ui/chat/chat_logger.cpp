// src/ui/chat/chat_logger.cpp
#include "chat_logger.hpp"
#include <cstdlib>
#include <filesystem>
#include <QString>
#include <QDateTime>

namespace progressive::desktop {

void ChatLogger::start(const std::string& roomId, const std::string& roomName) {
    stop();
    roomId_ = roomId;
    const char* xdg = getenv("XDG_DATA_HOME");
    std::string base;
    if (xdg && xdg[0]) base = xdg;
    else {
        const char* home = getenv("HOME");
        base = home ? std::string(home) + "/.local/share" : "/tmp";
    }
    base += "/progressive-desktop/chat_logs";
    std::filesystem::create_directories(base);
    auto ts = QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss").toStdString();
    std::string path = base + "/" + roomId_ + "_" + ts + ".log";
    file_ = std::make_unique<std::ofstream>(path, std::ios::app);
    if (file_->is_open()) {
        *file_ << "=== " << roomName << " === " << ts << " ===\n\n";
    }
}

void ChatLogger::stop() {
    file_.reset();
}

void ChatLogger::log(const std::string& line) {
    if (file_ && file_->is_open()) {
        *file_ << line << "\n";
        file_->flush();
    }
}

} // namespace
