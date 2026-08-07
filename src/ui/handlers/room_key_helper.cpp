// src/ui/handlers/room_key_helper.cpp
#include "room_key_helper.hpp"
#include "core/matrix_client.hpp"
#include "core/crypto/decryptor.hpp"
#include "core/debug_log.hpp"

#include <simdjson.h>
#include <vector>
#include <string>

namespace progressive::desktop {

bool shareRoomKeyForRoom(MatrixClient& client, Decryptor& dec,
                           const std::string& roomId) {
    if (dec.roomKeyShared(roomId)) return true;

    std::string ourUserId = client.account().userId;
    std::string ourDeviceId = client.account().deviceId;
    std::string homeserver = client.account().homeserverUrl;
    std::string token = client.account().accessToken;

    auto membersResp = client.getRoomMembers(roomId, true);
    if (!membersResp.ok) return false;

    std::vector<std::string> userIds;
    simdjson::dom::parser mp;
    auto doc = mp.parse(membersResp.data);
    if (doc.error() == simdjson::SUCCESS) {
        auto chunk = doc.value()["chunk"].get_array();
        if (chunk.error() == simdjson::SUCCESS) {
            for (auto evt : chunk.value()) {
                auto mship = evt["content"]["membership"].get_string();
                if (mship.error() != simdjson::SUCCESS ||
                    std::string(mship.value()) != "join") continue;
                auto sk = evt["state_key"].get_string();
                if (sk.error() == simdjson::SUCCESS)
                    userIds.push_back(std::string(sk.value()));
            }
        }
    }

    if (userIds.empty()) return false;

    bool shared = dec.shareRoomKey(roomId, userIds, ourUserId, ourDeviceId,
                                    homeserver, token);
    if (shared) dec.markRoomKeyShared(roomId);

    LOG(LogChannel::E2EE, "shareRoomKeyForRoom: room=%.30s shared=%d",
        roomId.c_str(), shared ? 1 : 0);
    return shared;
}

} // namespace progressive::desktop
