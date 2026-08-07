#include <olm/outbound_group_session.h>
#include <olm/inbound_group_session.h>
#include <olm/olm.h>
#include <progressive/olm.hpp>
#include "core/crypto/megolm_store.hpp"
#include "core/crypto/decryptor.hpp"
#include <simdjson.h>
#include <iostream>
#include <string>
#include <cstring>
#include <vector>
#include <cstdint>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::cerr << "FAIL: " << msg << " (line " << __LINE__ << ")\n"; failures++; } \
    else { std::cout << "ok: " << msg << "\n"; } \
} while (0)
#define CHECK_EQ(a, b, msg) do { \
    if ((a) != (b)) { std::cerr << "FAIL: " << msg << " (expected " << (b) << " got " << (a) << ") line " << __LINE__ << "\n"; failures++; } \
    else { std::cout << "ok: " << msg << "\n"; } \
} while (0)

static int my_rand() {
    static int x = 12345;
    x = x * 1103515245 + 12345;
    return (x >> 16) & 0x7fff;
}
static void fill_random(uint8_t* buf, size_t len) {
    for (size_t i = 0; i < len; ++i) buf[i] = (uint8_t)(my_rand() & 0xff);
}

static std::string extractFirstOTK(const std::string& json) {
    auto pos = json.find("\"AAAAAA\":\"");
    if (pos == std::string::npos) {
        pos = json.find("\"curve25519\":{");
        if (pos == std::string::npos) return "";
        pos = json.find("\":\"", pos);
        if (pos == std::string::npos) return "";
        pos += 3;
    } else {
        pos += 10;
    }
    auto end = json.find('"', pos);
    if (end == std::string::npos) return "";
    return json.substr(pos, end - pos);
}

// ---- Test 1: Megolm inbound (raw C → MegolmStore) ----
// Catches #1 (wrong fn), #1b (double-decode), #2 (buffer clobber), #7 (session zeroing)

static void test_megolm_roundtrip() {
    using namespace progressive::desktop;

    size_t size = ::olm_outbound_group_session_size();
    std::vector<uint8_t> memory(size);
    ::OlmOutboundGroupSession* outSession = ::olm_outbound_group_session(memory.data());

    size_t rnd = ::olm_init_outbound_group_session_random_length(outSession);
    std::vector<uint8_t> rndBuf(rnd);
    fill_random(rndBuf.data(), rnd);
    size_t rc = ::olm_init_outbound_group_session(outSession, rndBuf.data(), rnd);
    CHECK(rc != ::olm_error(), "Outbound session created");

    size_t keyLen = ::olm_outbound_group_session_key_length(outSession);
    std::vector<uint8_t> keyBuf(keyLen);
    rc = ::olm_outbound_group_session_key(outSession, keyBuf.data(), keyLen);
    CHECK(rc != ::olm_error(), "Got outbound session key");
    std::string sessionKey(keyBuf.begin(), keyBuf.begin() + rc);

    size_t idLen = ::olm_outbound_group_session_id_length(outSession);
    std::vector<uint8_t> idBuf(idLen);
    rc = ::olm_outbound_group_session_id(outSession, idBuf.data(), idLen);
    CHECK(rc != ::olm_error(), "Got outbound session ID");
    std::string sessionId(idBuf.begin(), idBuf.begin() + rc);

    std::string plaintext = "Hello Megolm!";
    size_t msgLen = ::olm_group_encrypt_message_length(outSession, plaintext.size());
    std::vector<uint8_t> msg(msgLen);
    rc = ::olm_group_encrypt(outSession,
        (const uint8_t*)plaintext.data(), plaintext.size(),
        msg.data(), msgLen);
    CHECK(rc != ::olm_error(), "Encrypt");
    std::string ciphertext(msg.begin(), msg.begin() + rc);

    std::string roomId = "!test:localhost";
    std::string senderKey = "FAKEsenderKeyAAAAAAAAAAAAA";
    MegolmStore store;
    bool added = store.addInboundSession(roomId, senderKey, sessionId, sessionKey);
    CHECK(added, "addInboundSession");
    CHECK(store.hasSession(roomId, senderKey, sessionId), "hasSession after add");
    CHECK(!store.hasSession(roomId, senderKey, "WRONG"), "hasSession with wrong id");

    std::string decrypted = store.decrypt(roomId, senderKey, sessionId, ciphertext);
    CHECK(!decrypted.empty(), "Decrypt returned non-empty");
    CHECK_EQ(decrypted, plaintext, "Plaintext matches!");

    ::olm_clear_outbound_group_session(outSession);
    std::cout << "--- test_megolm_roundtrip PASSED ---\n";
}

// ---- Test 2: Olm wrapper roundtrip (progressive::OlmSession) ----
// Catches #8 (Olm decrypt buffer clobber in OlmSession::decrypt)
// Covers both type 0 (pre-key) and type 1 (normal message) paths.

static void test_olm_wrapper_roundtrip() {
    using namespace progressive;

    // Bob creates account + OTK
    progressive::OlmAccount bobAccount;
    auto c = bobAccount.create();
    CHECK(c.success, "Bob account created");

    auto otk = bobAccount.generateOneTimeKeys(5);
    CHECK(otk.success, "Bob generated OTKs");

    std::string b64Key = extractFirstOTK(otk.data);
    CHECK(!b64Key.empty(), "Extracted first OTK");

    auto bobCurve = bobAccount.curve25519Key();
    CHECK(bobCurve.success, "Got Bob curve25519");

    // Alice creates outbound session + encrypts type 0
    progressive::OlmAccount aliceAccount;
    auto a = aliceAccount.create();
    CHECK(a.success, "Alice account created");

    progressive::OlmSession aliceSession;
    auto out = aliceSession.createOutbound(aliceAccount, bobCurve.data, b64Key);
    CHECK(out.success, "Alice createOutbound");

    std::string original = "Hello from Alice!";
    auto enc = aliceSession.encrypt(original);
    CHECK(enc.success, "Alice encrypt (type 0)");
    CHECK_EQ(enc.messageType, 0, "Pre-key message type is 0");

    // Bob creates inbound + decrypts — THIS exercises OlmSession::decrypt (#8)
    std::string encCopy = enc.data;
    progressive::OlmSession bobSession;
    auto in = bobSession.createInbound(bobAccount, encCopy);
    CHECK(in.success, "Bob createInbound");

    encCopy = enc.data;
    auto dec = bobSession.decrypt(encCopy, 0);
    CHECK(dec.success, "Bob decrypt (type 0)");
    CHECK_EQ(dec.data, original, "Plaintext matches (type 0)!");

    // Alice encrypts again — exercises second decrypt path (wrapper reports type)
    auto enc2 = aliceSession.encrypt("Hello again!");
    CHECK(enc2.success, "Alice encrypt (msg 2)");

    encCopy = enc2.data;
    auto dec2 = bobSession.decrypt(encCopy, enc2.messageType);
    CHECK(dec2.success, "Bob decrypt (msg 2)");
    CHECK_EQ(dec2.data, "Hello again!", "Plaintext matches (msg 2)!");

    std::cout << "--- test_olm_wrapper_roundtrip PASSED ---\n";
}

// ---- Test 3: Outbound encrypt → Inbound decrypt roundtrip ----
// Catches #5 (double base64-encode) and #6 (session zeroing)

static void test_outbound_encrypt_roundtrip() {
    using namespace progressive::desktop;

    Decryptor decryptor;
    bool ok = decryptor.init();
    CHECK(ok, "Decryptor initialized");

    std::string roomId = "!testout:localhost";
    std::string deviceId = "TESTDEVICE";
    std::string plaintextBody = "{\"body\":\"Hello outbound!\",\"msgtype\":\"m.text\"}";

    // 1. Create outbound session + encrypt
    std::string sessionId = decryptor.getOrCreateOutboundSession(roomId);
    CHECK(!sessionId.empty(), "getOrCreateOutboundSession");
    std::string encryptedJson = decryptor.encryptMessage(roomId, deviceId, plaintextBody);
    CHECK(!encryptedJson.empty(), "encryptMessage returned JSON");

    // 2. Parse encrypted JSON
    simdjson::dom::parser parser;
    auto doc = parser.parse(encryptedJson);
    CHECK(doc.error() == simdjson::SUCCESS, "Parse encrypted JSON");
    auto v = doc.value();
    auto ct = v["ciphertext"].get_string();
    auto sid = v["session_id"].get_string();
    auto sk = v["sender_key"].get_string();
    CHECK(ct.error() == simdjson::SUCCESS, "Ciphertext extracted");
    CHECK(sid.error() == simdjson::SUCCESS, "Session ID extracted");
    CHECK(sk.error() == simdjson::SUCCESS, "Sender key extracted");
    std::string ciphertext(ct.value());
    std::string parsedSessionId(sid.value());
    std::string senderKey(sk.value());

    // 3. Get session key
    std::string sessionKey = decryptor.getOutboundSessionKey(roomId);
    CHECK(!sessionKey.empty(), "getOutboundSessionKey");

    // 4. Add inbound session + decrypt
    MegolmStore store;
    bool added = store.addInboundSession(roomId, senderKey, parsedSessionId, sessionKey);
    CHECK(added, "addInboundSession for outbound test");
    std::string decrypted = store.decrypt(roomId, senderKey, parsedSessionId, ciphertext);
    CHECK(!decrypted.empty(), "Decrypt returned non-empty");
    CHECK_EQ(decrypted, plaintextBody, "Outbound encrypt roundtrip matches!");

    std::cout << "--- test_outbound_encrypt_roundtrip PASSED ---\n";
}

// ---- Test 4: sender_key drift — decrypt falls back to (room, session_id) ----
// Nheko keys megolm sessions by (room, session_id) only. After an identity
// reset our events carry a NEW sender_key while sessions are stored under the
// old one — the strict (room, sender_key, session_id) lookup must fall back.

static void test_sender_key_drift_fallback() {
    using namespace progressive::desktop;

    Decryptor decryptor;
    bool ok = decryptor.init();
    CHECK(ok, "Decryptor initialized");

    std::string roomId = "!drift:localhost";
    std::string deviceId = "TESTDEVICE";
    std::string plaintextBody = "{\"body\":\"drift test\",\"msgtype\":\"m.text\"}";

    std::string sessionId = decryptor.getOrCreateOutboundSession(roomId);
    CHECK(!sessionId.empty(), "getOrCreateOutboundSession");
    std::string encryptedJson = decryptor.encryptMessage(roomId, deviceId, plaintextBody);
    CHECK(!encryptedJson.empty(), "encryptMessage returned JSON");

    simdjson::dom::parser parser;
    auto doc = parser.parse(encryptedJson);
    CHECK(doc.error() == simdjson::SUCCESS, "Parse encrypted JSON");
    auto v = doc.value();
    auto ct = v["ciphertext"].get_string();
    auto sid = v["session_id"].get_string();
    CHECK(ct.error() == simdjson::SUCCESS && sid.error() == simdjson::SUCCESS,
          "Extract ciphertext + session_id");
    std::string ciphertext(ct.value());
    std::string parsedSessionId(sid.value());
    std::string sessionKey = decryptor.getOutboundSessionKey(roomId);
    CHECK(!sessionKey.empty(), "getOutboundSessionKey");

    // Store under a DIFFERENT (old) sender key — simulates a session received
    // before our identity change.
    MegolmStore store;
    bool added = store.addInboundSession(roomId, "OLDKEYAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
                                         parsedSessionId, sessionKey);
    CHECK(added, "addInboundSession under OLD sender key");

    // Event arrives with a NEW sender key — strict lookup misses, fallback must hit.
    std::string decrypted = store.decrypt(roomId, "NEWKEYBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB",
                                          parsedSessionId, ciphertext);
    CHECK(!decrypted.empty(), "Sender-key drift fallback decrypts");
    CHECK_EQ(decrypted, plaintextBody, "Drift fallback plaintext matches");

    std::cout << "--- test_sender_key_drift_fallback PASSED ---\n";
}

// ---- Test 5: outbound sessions are discarded when the identity changes ----
// Sessions from a previous identity must never survive a restart: the pickle
// carries the creating senderKey and unpickle drops mismatched entries.

static void test_outbound_discard_on_identity_change() {
    using namespace progressive::desktop;

    Decryptor decryptor;
    bool ok = decryptor.init();
    CHECK(ok, "Decryptor initialized");

    std::string roomId = "!identity:localhost";
    std::string oldId = decryptor.getOrCreateOutboundSession(roomId);
    CHECK(!oldId.empty(), "Session created under old identity");

    std::string pickled = decryptor.pickleOutboundSessions("testkey");
    CHECK(pickled.find("\"senderKey\":\"") != std::string::npos,
          "Pickle records the creating senderKey");

    // Identity reset (clears in-memory outbound sessions).
    CHECK(decryptor.resetIdentity(), "resetIdentity");
    // Reload the OLD pickle — the session belongs to the previous identity and
    // must be discarded, so a fresh session with a NEW id is created.
    CHECK(decryptor.unpickleOutboundSessions("testkey", pickled),
          "unpickleOutboundSessions (discards stale)");
    std::string newId = decryptor.getOrCreateOutboundSession(roomId);
    CHECK(!newId.empty(), "Fresh session after reset");
    CHECK(newId != oldId, "Old-identity session was discarded (new id differs)");

    // And the fresh session is pickled under the CURRENT sender key.
    std::string pickled2 = decryptor.pickleOutboundSessions("testkey");
    CHECK(pickled2.find(decryptor.curve25519Key()) != std::string::npos,
          "New pickle carries the current sender key");

    std::cout << "--- test_outbound_discard_on_identity_change PASSED ---\n";
}

int main() {
    std::cout << "=== Megolm + Olm Roundtrip Tests ===\n\n";
    test_megolm_roundtrip();
    test_olm_wrapper_roundtrip();
    test_outbound_encrypt_roundtrip();
    test_sender_key_drift_fallback();
    test_outbound_discard_on_identity_change();
    if (failures == 0) { std::cout << "\nALL TESTS PASSED\n"; return 0; }
    std::cerr << failures << " FAILURE(S)\n"; return 1;
}
