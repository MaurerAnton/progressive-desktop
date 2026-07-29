// src/ui/handlers/room_key_helper.hpp — shared room key helper.
#pragma once
#include <string>

namespace progressive::desktop {

class MatrixClient;
class Decryptor;

// Shares the outbound Megolm room_key for `roomId` with all joined members.
// Reads credentials FRESH from client.account() at call time (AGENTS.md:
// never cache mutable credentials — the ctxToken_ bug).
// Returns true if the key was shared (or already shared).
bool shareRoomKeyForRoom(MatrixClient& client, Decryptor& dec,
                          const std::string& roomId);

} // namespace progressive::desktop
