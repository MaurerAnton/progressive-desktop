// src/ui/room_view.cpp — room loading helpers.
// TODO: extract loadRoomHistory and loadMemberAvatarsForRoom from main_window.cpp.
// These require access to private members (timelineModel_, client_, etc.).
// Implementation in main_window.cpp for now.

#include "room_view.hpp"
#include <cstdio>

namespace progressive::desktop {

void roomLoadHistory(MainWindow* win, const std::string& roomId) {
    (void)win;
    (void)roomId;
    // See MainWindow::loadRoomHistory in main_window.cpp
}

void roomLoadMemberAvatars(MainWindow* win, const std::string& roomId) {
    (void)win;
    (void)roomId;
    // See MainWindow::loadMemberAvatarsForRoom in main_window.cpp
}

} // namespace progressive::desktop
