// tests/test_synapse_e2ee.cpp — live-Synapse E2EE integration test.
//
// Drives the REAL cross-account encrypted-message flow against a running
// Synapse homeserver (matrixdotorg/synapse docker image, registration enabled):
//   register A + B → upload device keys → A creates encrypted room + invites B
//   → B joins → A shares room key + sends m.room.encrypted → B syncs,
//   decrypts Olm-wrapped m.room_key, then decrypts the Megolm timeline event.
//
// Regression guard for: "cross-user E2EE key sharing is unreliable in rooms
// with 3+ members" and Olm/Megolm round-trip bugs. Runs in CI via the
// synapse-e2ee workflow; skipped locally when no server is reachable.
//
// Env: SYNAPSE_URL (default http://localhost:8008)
//
// Build + run (with a Synapse on :8008):
//   cmake --build build -j4 && SYNAPSE_URL=http://localhost:8008 ./build/test_synapse_e2ee

#include "core/http_client.hpp"
#include "core/matrix_client.hpp"
#include "core/crypto/decryptor.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <simdjson.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::cerr << "FAIL: " << msg << " (line " << __LINE__ << ")\n"; failures++; } \
    else { std::cout << "ok: " << msg << "\n"; } \
} while (0)

using namespace progressive::desktop;

static const char* envOr(const char* name, const char* def) {
    const char* v = std::getenv(name);
    return (v && *v) ? v : def;
}

// ---- Small JSON helpers (no progressive_native parser dependency) ----

static std::string jsonStr(const simdjson::dom::element& el, const char* key) {
    auto v = el[key].get_string();
    if (v.error() == simdjson::SUCCESS) return std::string(v.value());
    return {};
}

// ---- Test user harness: registers + sets up E2EE, mirrors the app ----

struct TestUser {
    MatrixClient client;
    Decryptor decryptor;
    std::string userId;
    std::string deviceId;
    std::string token;
};

static bool registerUser(TestUser& u, const std::string& hs, const std::string& uname,
                         const std::string& pass) {
    auto r = u.client.registerAccount(uname, pass, hs);
    if (!r.ok) {
        std::cerr << "[synapse-test] register " << uname << " failed: "
                  << r.error.message << "\n";
        return false;
    }
    u.userId = r.data.userId;
    u.deviceId = r.data.deviceId;
    u.token = r.data.accessToken;
    u.client.setAccount(r.data);
    return true;
}

static bool setupE2EE(TestUser& u, const std::string& hs) {
    if (!u.decryptor.init()) {
        std::cerr << "[synapse-test] decryptor init failed for " << u.userId << "\n";
        return false;
    }
    u.decryptor.setCryptoContext(u.userId, u.deviceId, hs, u.token);
    std::string body = u.decryptor.buildKeysUploadBody(u.userId, u.deviceId, 10, true);
    auto up = u.client.uploadKeys(body);
    if (!up.ok) {
        std::cerr << "[synapse-test] keys/upload failed for " << u.userId << ": "
                  << up.error.message << "\n";
        return false;
    }
    u.decryptor.markOneTimeKeysPublished();
    u.decryptor.markAccountAsShared();
    return true;
}

// Joined member user IDs for a room (same parsing as room_key_helper.cpp).
static std::vector<std::string> joinedMembers(MatrixClient& client, const std::string& roomId) {
    std::vector<std::string> userIds;
    auto m = client.getRoomMembers(roomId);
    if (!m.ok) return userIds;
    simdjson::dom::parser p;
    auto doc = p.parse(m.data);
    if (doc.error() != simdjson::SUCCESS) return userIds;
    auto chunk = doc.value()["chunk"].get_array();
    if (chunk.error() != simdjson::SUCCESS) return userIds;
    for (auto evt : chunk.value()) {
        auto mship = evt["content"]["membership"].get_string();
        if (mship.error() != simdjson::SUCCESS || std::string(mship.value()) != "join") continue;
        auto sk = evt["state_key"].get_string();
        if (sk.error() == simdjson::SUCCESS) userIds.push_back(std::string(sk.value()));
    }
    return userIds;
}

// Dedicated fresh user uploads exactly 1 OTK + fallback; Bob claims twice —
// 1st returns the OTK, 2nd returns THE fallback (verified by value match).

// Key-request loop: alice rotates (new session NOT shared) -> bob fails to
// decrypt msg2 (pending + requests key) -> alice's sync handles the request +
// forwards m.forwarded_room_key -> bob imports + processPending re-decrypts.
static bool test_key_request_loop(const std::string& hs,
                                   TestUser& alice, TestUser& bob,
                                   const std::string& roomId,
                                   const std::string& aliceSince0,
                                   std::string bobSince) {
    // 1. Alice: rotate to a fresh outbound session (do NOT share it with bob).
    alice.decryptor.setRoomEncryptionConfig(roomId,
        "{\"algorithm\":\"m.megolm.v1.aes-sha2\",\"rotation_period_msgs\":1}");
    alice.decryptor.getOrCreateOutboundSession(roomId);
    std::string body2 = "hello-rot2-" + std::to_string(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    std::string inner2 = "{\"type\":\"m.room.message\",\"content\":{\"msgtype\":\"m.text\",\"body\":\""
                         + body2 + "\"},\"room_id\":\"" + roomId + "\"}";
    std::string enc2 = alice.decryptor.encryptMessage(roomId, alice.deviceId, inner2);
    CHECK(!enc2.empty(), "kr: alice encrypted with rotated session");
    std::string txnId2 = "synapse-kr-" + std::to_string(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    auto send2 = alice.client.sendEncryptedEvent(roomId, enc2, txnId2);
    CHECK(send2.ok, "kr: alice sent rotated message");

    // 2. Bob: sync -> msg2 fails to decrypt (no session2) -> pending + request.
    bool requested = false;
    for (int round = 0; round < 8 && !requested; ++round) {
        auto resp = bob.client.syncFast(bobSince, 3000, false);
        if (!resp.ok) { std::this_thread::sleep_for(std::chrono::milliseconds(500)); continue; }
        bobSince = std::string(resp.data.nextBatch);
        for (const auto& [rid, room] : resp.data.joinedRooms) {
            if (rid != roomId) continue;
            for (const auto& evt : room.timeline.events) {
                if (!evt.isEncrypted()) continue;
                auto dec = bob.decryptor.decryptMegolmEvent(
                    roomId, std::string(evt.senderId), std::string(evt.contentJson),
                    std::string(evt.eventId), evt.originServerTs);
                if (dec.error.find("no megolm session") != std::string::npos) requested = true;
            }
        }
        if (!requested) std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    CHECK(requested, "kr: bob failed to decrypt + requested the key");

    // 3. Alice: sync -> m.room_key_request -> forward the key.
    std::string aliceSince = aliceSince0;
    bool forwarded = false;
    for (int round = 0; round < 8 && !forwarded; ++round) {
        auto resp = alice.client.syncFast(aliceSince, 3000, false);
        if (!resp.ok) { std::this_thread::sleep_for(std::chrono::milliseconds(500)); continue; }
        aliceSince = std::string(resp.data.nextBatch);
        for (const auto& evt : resp.data.toDeviceEventList) {
            if (evt.type != "m.room.encrypted") continue;
            std::string inner = alice.decryptor.handleOlmEncryptedToDevice(
                std::string(evt.senderId), std::string(evt.contentJson));
            if (inner.find("m.room_key_request") != std::string::npos) forwarded = true;
        }
        if (!forwarded) std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    CHECK(forwarded, "kr: alice forwarded the key");

    // 4. Bob: sync -> m.forwarded_room_key -> import + processPending -> decrypt.
    bool redecrypted = false;
    for (int round = 0; round < 8 && !redecrypted; ++round) {
        auto resp = bob.client.syncFast(bobSince, 3000, false);
        if (!resp.ok) { std::this_thread::sleep_for(std::chrono::milliseconds(500)); continue; }
        bobSince = std::string(resp.data.nextBatch);
        for (const auto& evt : resp.data.toDeviceEventList) {
            if (evt.type == "m.room.encrypted") {
                bob.decryptor.handleOlmEncryptedToDevice(
                    std::string(evt.senderId), std::string(evt.contentJson));
            }
        }
        for (const auto& d : bob.decryptor.takeDecryptedEvents()) {
            if (d.plaintext.find(body2) != std::string::npos) redecrypted = true;
        }
        if (!redecrypted) std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    CHECK(redecrypted, "kr: bob re-decrypted the rotated message");
    return true;
}


static bool test_fallback_claim(const std::string& hs, TestUser& bob) {
    // Fresh dedicated user — no prior OTK pool (Synapse ADDS OTKs on upload,
    // so a fresh account is required to make the pool deterministic: exactly 1 OTK).
    TestUser fb;
    if (!registerUser(fb, hs, "synapse_fb_user", "synapse_test_pass_42")) return false;
    if (!fb.decryptor.init()) {
        std::cerr << "[synapse-test] fb decryptor init failed\n";
        return false;
    }
    fb.decryptor.setCryptoContext(fb.userId, fb.deviceId, hs, fb.token);

    // Build body: 1 OTK + device_keys + FALLBACK (includeFallbackKey=true).
    std::string body = fb.decryptor.buildKeysUploadBody(
        fb.userId, fb.deviceId, 1, true, true);
    // The fallback was generated but is UNPUBLISHED here — capture its value.
    std::string fbJson = fb.decryptor.account()->unpublishedFallbackKey();
    std::string expectedFallback;
    {
        simdjson::dom::parser p;
        auto doc = p.parse(fbJson);
        if (doc.error() == simdjson::SUCCESS) {
            auto root = doc.value().get_object();
            if (root.error() == simdjson::SUCCESS) {
                for (auto f : root.value()) {
                    auto inner = f.value.get_object();
                    if (inner.error() != simdjson::SUCCESS) continue;
                    for (auto k : inner.value()) {
                        auto kv = k.value.get_string();
                        if (kv.error() == simdjson::SUCCESS) expectedFallback = std::string(kv.value());
                    }
                }
            }
        }
    }
    CHECK(!expectedFallback.empty(), "fb-synapse: captured expected fallback value");
    CHECK((int)expectedFallback.size() == 43, "fb-synapse: expected fallback is 43-char base64");

    auto up = fb.client.uploadKeys(body);
    CHECK(up.ok, "fb-synapse: 1 OTK + fallback uploaded");
    if (!up.ok) return false;
    fb.decryptor.markOneTimeKeysPublished();
    fb.decryptor.markAccountAsShared();

    // Bob: claim twice — 1st returns the OTK, 2nd returns THE fallback.
    std::string claimBody = "{\"one_time_keys\":{\"" + fb.userId
        + "\":{\"" + fb.deviceId + "\":\"signed_curve25519\"}}}";
    std::string firstKey, secondKey;
    for (int claim = 0; claim < 2; ++claim) {
        auto resp = bob.client.claimKeys(claimBody);
        CHECK(resp.ok, ("fb-synapse: claim " + std::to_string(claim + 1) + " OK").c_str());
        if (!resp.ok) continue;
        simdjson::dom::parser p;
        auto doc = p.parse(resp.data);
        if (doc.error() != simdjson::SUCCESS) continue;
        auto otk = doc.value()["one_time_keys"].get_object();
        if (otk.error() != simdjson::SUCCESS) continue;
        auto userDevs = otk.value()[fb.userId].get_object();
        if (userDevs.error() != simdjson::SUCCESS) continue;
        auto devKeys = userDevs.value()[fb.deviceId].get_object();
        if (devKeys.error() != simdjson::SUCCESS) continue;
        for (auto k : devKeys.value()) {
            std::string kk(k.key);
            if (kk.find("signed_curve25519:") != 0) continue;
            auto keyObj = k.value.get_object();
            if (keyObj.error() != simdjson::SUCCESS) continue;
            auto kv = keyObj.value()["key"].get_string();
            if (kv.error() != simdjson::SUCCESS) continue;
            if (claim == 0) firstKey = std::string(kv.value());
            else secondKey = std::string(kv.value());
        }
    }
    CHECK(!firstKey.empty(), "fb-synapse: 1st claim returned a key");
    CHECK(!secondKey.empty(), "fb-synapse: 2nd claim returned a key");
    CHECK(secondKey == expectedFallback,
          "fb-synapse: 2nd claim returned THE uploaded fallback key (value match)");
    CHECK(firstKey != secondKey, "fb-synapse: fallback differs from the OTK");
    std::cout << "  fb-synapse: fallback claim verified, key=" << secondKey.substr(0, 8) << "...\n";
    return true;
}

int main() {
    std::cout << "=== Synapse E2EE Integration Test ===\n\n";
    httpInit();

    std::string hs = envOr("SYNAPSE_URL", "http://localhost:8008");
    std::string pass = "synapse_test_pass_42";

    // Wait for the server (graceful skip locally).
    bool reachable = false;
    for (int i = 0; i < 5; ++i) {
        auto v = httpGet(hs + "/_matrix/client/versions", {}, 3000);
        if (v.isOk()) { reachable = true; break; }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    if (!reachable) {
        std::cout << "SKIP: no Synapse reachable at " << hs << " (set SYNAPSE_URL)\n";
        httpCleanup();
        return 0;
    }
    std::cout << "server up: " << hs << "\n";

    TestUser alice;
    TestUser bob;
    if (!registerUser(alice, hs, "synapse_alice", pass)) return 1;
    if (!registerUser(bob, hs, "synapse_bob", pass)) return 1;
    std::cout << "registered alice=" << alice.userId << " bob=" << bob.userId << "\n";
    CHECK(alice.deviceId != bob.deviceId, "distinct device IDs");

    if (!setupE2EE(alice, hs) || !setupE2EE(bob, hs)) return 1;
    std::cout << "device keys uploaded for both\n";

    // Alice creates an encrypted room and invites Bob.
    auto roomRes = alice.client.createRoom("synapse-integration", "", false,
                                           {bob.userId}, true);
    if (!roomRes.ok) {
        std::cerr << "[synapse-test] createRoom failed: " << roomRes.error.message << "\n";
        return 1;
    }
    std::string roomId = roomRes.data;
    std::cout << "alice created encrypted room: " << roomId << "\n";

    auto joinRes = bob.client.joinRoom(roomId);
    if (!joinRes.ok) {
        std::cerr << "[synapse-test] bob join failed: " << joinRes.error.message << "\n";
        return 1;
    }
    std::cout << "bob joined room\n";

    // Bob's first sync: registers the room + device list.
    auto sync0 = bob.client.syncFast("", 5000, false);
    std::string since0 = sync0.ok ? std::string(sync0.data.nextBatch) : "";
    (void)since0;

    // Alice: build outbound Megolm session, share room key, send encrypted message.
    std::string sessId = alice.decryptor.getOrCreateOutboundSession(roomId);
    CHECK(!sessId.empty(), "alice has an outbound megolm session");

    std::vector<std::string> members = joinedMembers(alice.client, roomId);
    CHECK(!members.empty(), "room has joined members");
    bool keyShared = alice.decryptor.shareRoomKey(roomId, members,
        alice.userId, alice.deviceId, hs, alice.token);
    if (keyShared) alice.decryptor.markRoomKeyShared(roomId);
    CHECK(keyShared, "room key shared to members");

    std::string body = "hello-synapse-e2ee-" + std::to_string(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    std::string inner = "{\"type\":\"m.room.message\",\"content\":{\"msgtype\":\"m.text\",\"body\":\""
                        + body + "\"},\"room_id\":\"" + roomId + "\"}";
    std::string enc = alice.decryptor.encryptMessage(roomId, alice.deviceId, inner);
    CHECK(!enc.empty(), "alice encrypts the message");
    std::string txnId = "synapse-test-" + std::to_string(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    auto sendRes = alice.client.sendEncryptedEvent(roomId, enc, txnId);
    CHECK(sendRes.ok, "alice sends m.room.encrypted");
    std::cout << "alice sent event: " << sendRes.data << "\n";

    // Bob: sync loop — process to-device (Olm room_key) then decrypt the timeline.
    bool decrypted = false;
    std::string plaintext;
    std::string since = since0;
    for (int round = 0; round < 10 && !decrypted; ++round) {
        auto resp = bob.client.syncFast(since, 3000, false);
        if (!resp.ok) {
            std::cerr << "[synapse-test] bob sync failed round " << round << ": "
                      << resp.error.message << "\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }
        since = std::string(resp.data.nextBatch);

        // 1. To-device events (m.room_key is Olm-wrapped m.room.encrypted).
        for (const auto& evt : resp.data.toDeviceEventList) {
            if (evt.type == "m.room.encrypted") {
                bob.decryptor.handleOlmEncryptedToDevice(
                    std::string(evt.senderId), std::string(evt.contentJson));
            } else if (evt.type == "m.room_key") {
                bob.decryptor.handleRoomKey(std::string(evt.contentJson));
            }
        }

        // 2. Timeline: decrypt m.room.encrypted events in our room.
        for (const auto& [rid, room] : resp.data.joinedRooms) {
            if (rid != roomId) continue;
            for (const auto& evt : room.timeline.events) {
                if (!evt.isEncrypted()) continue;
                auto dec = bob.decryptor.decryptMegolmEvent(
                    roomId, std::string(evt.senderId),
                    std::string(evt.contentJson),
                    std::string(evt.eventId), evt.originServerTs);
                if (dec.ok) {
                    plaintext = dec.plaintext;
                    decrypted = true;
                }
            }
        }
        if (!decrypted) std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    CHECK(decrypted, "bob decrypts alice's megolm event");
    if (decrypted) {
        std::cout << "bob plaintext: " << plaintext << "\n";
        CHECK(plaintext.find(body) != std::string::npos,
              "decrypted plaintext contains the message body");
    }

    // Key-request loop (rotation -> request -> forward -> re-decrypt).
    std::cout << "\n--- key request loop test ---\n";
    if (!test_key_request_loop(hs, alice, bob, roomId, since0, since)) failures++;
    std::cout << "--- key request loop done ---\n";

    std::cout << "\n";
    std::cout << "\n--- fallback claim test ---\n";
    if (!test_fallback_claim(hs, bob)) failures++;
    if (failures == 0) {
        std::cout << "=== ALL SYNAPSE E2EE TESTS PASSED ===" << std::endl;
        httpCleanup();
        return 0;
    }
    std::cerr << "=== " << failures << " TEST(S) FAILED ===" << std::endl;
    httpCleanup();
    return 1;
}
