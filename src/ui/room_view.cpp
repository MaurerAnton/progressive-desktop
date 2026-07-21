// src/ui/room_view.cpp — room loading helpers.
// NOTE: loadRoomHistory and loadMemberAvatarsForRoom are currently
// implemented as MainWindow methods in main_window.cpp. They access
// many private members (8+ fields) and cannot be cleanly extracted
// without a broader MainWindow refactor.
//
// When the refactor happens:
//   - MainWindow::loadRoomHistory → roomLoadHistory(client, model, ...)
//   - MainWindow::loadMemberAvatarsForRoom → roomLoadMemberAvatars(client, model, ...)
//
// The room_view.hpp header documents the target API.

#include "room_view.hpp"

namespace progressive::desktop {

void loadRoomHistory(MatrixClient*, TimelineModel*, QLabel*, QListView*,
                      std::string&, QPushButton*,
                      const std::unordered_map<std::string, std::string>&,
                      const std::string&) {
    // Implemented in MainWindow::loadRoomHistory (main_window.cpp)
}

void loadMemberAvatars(MatrixClient*, TimelineModel*,
                        std::unordered_map<std::string, std::string>&,
                        const std::string&) {
    // Implemented in MainWindow::loadMemberAvatarsForRoom (main_window.cpp)
}

} // namespace progressive::desktop
