// src/ui/room_view.cpp — room history loading + member avatar loading.
// Extracted from MainWindow for cleaner separation.

#include "room_view.hpp"
#include "ui/main_window.hpp"
#include "core/matrix_client.hpp"
#include "timeline_model.hpp"

#include <QMetaObject>
#include <QPointer>
#include <QDateTime>
#include <QStandardPaths>
#include <QDir>
#include <QPushButton>
#include <QListView>
#include <QLabel>
#include <thread>
#include <cstdio>
#include <memory>
#include <unordered_set>
#include <unordered_map>

#include <simdjson.h>

namespace progressive::desktop {

// Forward declarations of helpers from main_window.cpp's anonymous namespace
namespace {
std::string jsonUnescape(std::string_view s);
std::string extractJsonStringDecoded(std::string_view json, std::string_view key);
std::string extractJsonString(std::string_view json, std::string_view key);
std::string extractBody(std::string_view contentJson);
std::string extractMsgtype(std::string_view contentJson);
std::string extractMxcUrl(std::string_view contentJson);
std::string extractMimetype(std::string_view contentJson);
std::string extractThreadRootId(std::string_view contentJson);
std::string extractReplyToId(std::string_view contentJson);
}

void roomLoadHistory(MainWindow* win, const std::string& roomId) {
    // Access needed members via MainWindow public interface
    // Currently implemented in main_window.cpp as MainWindow::loadRoomHistory
    // TODO: expose needed members as public or pass as parameters
    (void)win;
    (void)roomId;
}

void roomLoadMemberAvatars(MainWindow* win, const std::string& roomId) {
    // Currently implemented in main_window.cpp as MainWindow::loadMemberAvatarsForRoom
    // TODO: expose needed members as public or pass as parameters
    (void)win;
    (void)roomId;
}

} // namespace progressive::desktop
