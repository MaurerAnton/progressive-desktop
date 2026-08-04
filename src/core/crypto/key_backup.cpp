// src/core/crypto/key_backup.cpp
#include "key_backup.hpp"

#include "backup_crypto.hpp"
#include "decryptor.hpp"
#include "../matrix_client.hpp"
#include "olm_account.hpp"
#include "recovery_key.hpp"
#include "../session_store.hpp"

#include <simdjson.h>
#include <vector>

namespace progressive::desktop {

std::string createKeyBackup(MatrixClient& client, SessionStore* store,
                            const std::string& userId) {
    std::string recoveryKey = generateRecoveryKey();
    if (recoveryKey.empty()) return "";
    auto pair = deriveBackupKey(recoveryKeySeed(recoveryKey));
    if (pair.publicKeyB64.empty()) return "";

    BackupVersionInfo info;
    info.algorithm = "m.megolm_backup.v1.curve25519-aes-sha2";
    info.publicKey = pair.publicKeyB64;
    auto resp = client.createRoomKeysVersion(buildBackupVersionBody(info));
    if (!resp.ok) return "";

    std::string version;
    {
        simdjson::dom::parser p;
        auto doc = p.parse(resp.data);
        if (doc.error() == simdjson::SUCCESS) {
            auto v = doc.value()["version"].get_string();
            if (v.error() == simdjson::SUCCESS) version = std::string(v.value());
        }
    }
    if (version.empty()) return "";

    if (store) {
        BackupInfo bi;
        bi.version = version;
        bi.recoveryKey = recoveryKey;
        bi.publicKey = pair.publicKeyB64;
        bi.algorithm = info.algorithm;
        store->saveBackupInfo(userId, bi);
    }
    return recoveryKey;
}

bool uploadKeyBackup(MatrixClient& client, Decryptor& decryptor,
                     const BackupInfo& info) {
    if (info.publicKey.empty()) return false;
    std::string all = decryptor.exportAllKeys();
    if (all.empty()) return false;

    simdjson::dom::parser p;
    auto doc = p.parse(all);
    if (doc.error() != simdjson::SUCCESS) return false;
    auto rooms = doc.value()["rooms"].get_object();
    if (rooms.error() != simdjson::SUCCESS) return false;

    // {"rooms":{"<roomId>":{"sessions":{"<sessionId>":<entry>}}}}
    std::ostringstream body;
    body << "{\"rooms\":{";
    bool firstRoom = true;
    for (auto room : rooms.value()) {
        std::string roomId(room.key);
        auto sessions = room.value["sessions"].get_array();
        if (sessions.error() != simdjson::SUCCESS) continue;
        std::string roomJson = "{\"sessions\":{";
        bool firstSess = true;
        for (auto sess : sessions.value()) {
            auto sid = sess["session_id"].get_string();
            auto skey = sess["session_key"].get_string();
            if (sid.error() != simdjson::SUCCESS || skey.error() != simdjson::SUCCESS) continue;
            std::string sd = encryptBackupSessionData(
                std::string(skey.value()), info.publicKey);
            if (sd.empty()) continue;
            if (!firstSess) roomJson += ",";
            firstSess = false;
            auto sKey = sess["sender_key"].get_string();
            std::string sKeyStr = (sKey.error() == simdjson::SUCCESS)
                ? std::string(sKey.value()) : "";
            roomJson += "\"" + std::string(sid.value()) + "\":"
                + buildBackupSessionEntry(sd, 0, sKeyStr);
        }
        if (firstSess) continue;  // no usable sessions in this room
        roomJson += "}}";
        if (!firstRoom) body << ",";
        firstRoom = false;
        body << "\"" << roomId << "\":" << roomJson;
    }
    body << "}}";

    auto resp = client.uploadRoomKeys(body.str(), info.version);
    return resp.ok;
}

int restoreKeyBackup(MatrixClient& client, Decryptor& decryptor,
                     const BackupInfo& info) {
    if (info.publicKey.empty() || info.recoveryKey.empty()) return 0;
    auto pair = deriveBackupKey(recoveryKeySeed(info.recoveryKey));
    if (pair.privateKeyB64.empty()) return 0;

    auto resp = client.getRoomKeys(info.version);
    if (!resp.ok) return 0;

    simdjson::dom::parser p;
    auto doc = p.parse(resp.data);
    if (doc.error() != simdjson::SUCCESS) return 0;
    auto rooms = doc.value()["rooms"].get_object();
    if (rooms.error() != simdjson::SUCCESS) return 0;

    int imported = 0;
    for (auto room : rooms.value()) {
        std::string roomId(room.key);
        auto sessions = room.value["sessions"].get_object();
        if (sessions.error() != simdjson::SUCCESS) continue;
        for (auto sess : sessions.value()) {
            auto sd = sess.value["session_data"].get_string();
            auto sKey = sess.value["sender_key"].get_string();
            if (sd.error() != simdjson::SUCCESS || sKey.error() != simdjson::SUCCESS)
                continue;
            std::string exportB64 = decryptBackupSessionData(
                std::string(sd.value()), pair.privateKeyB64);
            if (exportB64.empty()) continue;
            std::string realId = decryptor.importSingleSession(
                roomId, std::string(sKey.value()), exportB64);
            if (!realId.empty()) imported++;
        }
    }
    return imported;
}

} // namespace progressive::desktop
