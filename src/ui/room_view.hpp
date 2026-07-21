// src/ui/room_view.hpp — room loading helpers extracted from MainWindow.
#pragma once
#include <string>
#include <unordered_set>
#include <QPointer>

class QString;

namespace progressive::desktop {

class MainWindow;
class MatrixClient;

// Load room message history via /messages endpoint.
void roomLoadHistory(MainWindow* win, const std::string& roomId);

// Load member avatars for a room (only users in current timeline).
void roomLoadMemberAvatars(MainWindow* win, const std::string& roomId);

} // namespace progressive::desktop
