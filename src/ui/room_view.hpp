// src/ui/room_view.hpp — room loading helpers with explicit parameters.
#pragma once
#include <string>
#include <unordered_set>
#include <unordered_map>

namespace progressive::desktop {

class MatrixClient;
class TimelineModel;
class QPushButton;
class QLabel;
class QListView;

// Load room message history via /messages endpoint.
// All parameters are explicit — no MainWindow dependency.
void loadRoomHistory(MatrixClient* client, TimelineModel* model,
                      QLabel* statusLabel, QListView* view,
                      std::string& prevBatch, QPushButton* loadMoreBtn,
                      const std::unordered_map<std::string, std::string>& avatarCache,
                      const std::string& roomId);

// Load member avatars for a room (only users in current timeline).
// Results stored into avatarCache (non-const reference for mutation).
void loadMemberAvatars(MatrixClient* client, TimelineModel* model,
                        std::unordered_map<std::string, std::string>& avatarCache,
                        const std::string& roomId);

} // namespace progressive::desktop
