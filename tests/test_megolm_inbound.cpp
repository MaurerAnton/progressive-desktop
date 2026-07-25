#include <olm/outbound_group_session.h>
#include <olm/inbound_group_session.h>
#include <olm/olm.h>
#include "core/crypto/megolm_store.hpp"
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

static void test_megolm_roundtrip() {
    using namespace progressive::desktop;

    // 1. Create outbound Megolm session
    size_t size = ::olm_outbound_group_session_size();
    std::vector<uint8_t> memory(size);
    ::OlmOutboundGroupSession* outSession = ::olm_outbound_group_session(memory.data());

    size_t rnd = ::olm_init_outbound_group_session_random_length(outSession);
    std::vector<uint8_t> rndBuf(rnd);
    fill_random(rndBuf.data(), rnd);
    size_t rc = ::olm_init_outbound_group_session(outSession, rndBuf.data(), rnd);
    CHECK(rc != ::olm_error(), "Outbound session created");

    // 2. Get session key (base64)
    size_t keyLen = ::olm_outbound_group_session_key_length(outSession);
    std::vector<uint8_t> keyBuf(keyLen);
    rc = ::olm_outbound_group_session_key(outSession, keyBuf.data(), keyLen);
    CHECK(rc != ::olm_error(), "Got outbound session key");
    std::string sessionKey(keyBuf.begin(), keyBuf.begin() + rc);

    // 3. Get session ID (base64)
    size_t idLen = ::olm_outbound_group_session_id_length(outSession);
    std::vector<uint8_t> idBuf(idLen);
    rc = ::olm_outbound_group_session_id(outSession, idBuf.data(), idLen);
    CHECK(rc != ::olm_error(), "Got outbound session ID");
    std::string sessionId(idBuf.begin(), idBuf.begin() + rc);

    // 4. Encrypt plaintext
    std::string plaintext = "Hello Megolm!";
    size_t msgLen = ::olm_group_encrypt_message_length(outSession, plaintext.size());
    std::vector<uint8_t> msg(msgLen);
    rc = ::olm_group_encrypt(outSession,
        (const uint8_t*)plaintext.data(), plaintext.size(),
        msg.data(), msgLen);
    CHECK(rc != ::olm_error(), "Encrypt");
    std::string ciphertext(msg.begin(), msg.begin() + rc);

    // 5. Add inbound session via MegolmStore
    std::string roomId = "!test:localhost";
    std::string senderKey = "FAKEsenderKeyAAAAAAAAAAAAA";
    MegolmStore store;
    bool added = store.addInboundSession(roomId, senderKey, sessionId, sessionKey);
    CHECK(added, "addInboundSession");

    // 6. Verify hasSession
    CHECK(store.hasSession(roomId, senderKey, sessionId), "hasSession after add");
    CHECK(!store.hasSession(roomId, senderKey, "WRONG"), "hasSession with wrong id");

    // 7. Decrypt
    std::string decrypted = store.decrypt(roomId, senderKey, sessionId, ciphertext);
    CHECK(!decrypted.empty(), "Decrypt returned non-empty");
    CHECK_EQ(decrypted, plaintext, "Plaintext matches!");

    // Cleanup
    ::olm_clear_outbound_group_session(outSession);

    std::cout << "--- test_megolm_roundtrip PASSED ---\n";
}

int main() {
    std::cout << "=== Megolm Inbound Test ===\n\n";
    test_megolm_roundtrip();
    if (failures == 0) { std::cout << "\nALL TESTS PASSED\n"; return 0; }
    std::cerr << failures << " FAILURE(S)\n"; return 1;
}
