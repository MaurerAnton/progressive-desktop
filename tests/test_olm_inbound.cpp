#include <olm/olm.h>
#include <progressive/olm.hpp>
#include "core/crypto/olm_account.hpp"
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

static std::string errorStr(::OlmSession* s) {
    auto e = olm_session_last_error(s);
    return e ? e : "(no error)";
}

static std::string errorStr(::OlmAccount* a) {
    auto e = olm_account_last_error(a);
    return e ? e : "(no error)";
}

static int my_rand() {
    static int x = 12345;
    x = x * 1103515245 + 12345;
    return (x >> 16) & 0x7fff;
}
static void fill_random(uint8_t* buf, size_t len) {
    for (size_t i = 0; i < len; ++i) buf[i] = (uint8_t)(my_rand() & 0xff);
}

static void test_olm_roundtrip() {
    // Bob: create account
    std::vector<uint8_t> bob_acct_buf(::olm_account_size());
    ::OlmAccount* bobAcc = ::olm_account(bob_acct_buf.data());
    size_t rnd = ::olm_create_account_random_length(bobAcc);
    std::vector<uint8_t> rndBuf(rnd);
    fill_random(rndBuf.data(), rnd);
    int rc = ::olm_create_account(bobAcc, rndBuf.data(), rnd);
    CHECK(rc != ::olm_error(), "Bob account created");
    if (rc == ::olm_error()) std::cerr << "  err=" << errorStr(bobAcc) << "\n";

    // Bob: generate OTKs
    rnd = ::olm_account_generate_one_time_keys_random_length(bobAcc, 5);
    rndBuf.resize(rnd);
    fill_random(rndBuf.data(), rnd);
    rc = ::olm_account_generate_one_time_keys(bobAcc, 5, rndBuf.data(), rnd);
    CHECK(rc != ::olm_error(), "Bob generated OTKs");
    if (rc == ::olm_error()) std::cerr << "  err=" << errorStr(bobAcc) << "\n";

    // Get Bob's identity keys JSON
    size_t idLen = ::olm_account_identity_keys_length(bobAcc);
    std::vector<uint8_t> bobIdKeys(idLen);
    ::olm_account_identity_keys(bobAcc, bobIdKeys.data(), idLen);
    std::cout << "  Bob identity: " << std::string((char*)bobIdKeys.data(), idLen) << "\n";

    // Get Bob's OTK JSON
    size_t otkLen = ::olm_account_one_time_keys_length(bobAcc);
    std::vector<uint8_t> bobOtKeys(otkLen);
    ::olm_account_one_time_keys(bobAcc, bobOtKeys.data(), otkLen);
    std::cout << "  Bob OTKs: " << std::string((char*)bobOtKeys.data(), otkLen) << "\n";

    // Alice: create account
    std::vector<uint8_t> alice_acct_buf(::olm_account_size());
    ::OlmAccount* aliceAcc = ::olm_account(alice_acct_buf.data());
    rnd = ::olm_create_account_random_length(aliceAcc);
    rndBuf.resize(rnd);
    fill_random(rndBuf.data(), rnd);
    rc = ::olm_create_account(aliceAcc, rndBuf.data(), rnd);
    CHECK(rc != ::olm_error(), "Alice account created");

    // Alice: get identity keys for verification
    idLen = ::olm_account_identity_keys_length(aliceAcc);
    std::vector<uint8_t> aliceIdKeys(idLen);
    ::olm_account_identity_keys(aliceAcc, aliceIdKeys.data(), idLen);

    // Alice: create outbound session using Bob's base64 keys
    // b_id_keys.data() + 15 = curve25519 identity key base64 (43 chars)
    // b_ot_keys.data() + 25 = first OTK base64 (43 chars)
    // The "+15" skips: {"curve25519":"  (15 chars)
    // The "+25" skips: {"curve25519":{"AAAAAA":","AAAAAA":"  (25 chars... wait)

    // Bob's identity JSON: {"curve25519":"<b64>","ed25519":"<b64>"}
    // Bob's OTK JSON (nested): {"curve25519":{"AAAAqg":"<b64>","AAAAqQ":"<b64>"}}

    // For identity keys: +15 = skip {"curve25519":" → points to curve25519 b64
    // For OTK keys (nested): we need to find the first b64 value
    // {"curve25519":{" → 14 chars, then "keyID":" → varies, then b64 value

    // Extract Bob's curve25519 identity key base64 from JSON
    auto idStr = std::string((char*)bobIdKeys.data(), idLen);
    auto pos = idStr.find("\"curve25519\":\"");
    CHECK(pos != std::string::npos, "Find curve25519 in identity keys");
    auto ikStart = pos + 14;  // skip "curve25519":"
    auto ikEnd = idStr.find('"', ikStart);
    std::string bobIkB64 = idStr.substr(ikStart, ikEnd - ikStart);
    CHECK(bobIkB64.size() == 43, "Bob IK base64 is 43 chars");

    // Extract Bob's first OTK base64 from JSON (nested format)
    auto otkStr = std::string((char*)bobOtKeys.data(), otkLen);
    pos = otkStr.find("\"curve25519\":{");
    CHECK(pos != std::string::npos, "Find curve25519 obj in OTK JSON");
    pos = otkStr.find("\":\"", pos);  // skip to first key's value
    CHECK(pos != std::string::npos, "Find first OTK value");
    auto otkStart = pos + 3;  // skip ":"
    auto otkEnd = otkStr.find('"', otkStart);
    std::string bobOtkB64 = otkStr.substr(otkStart, otkEnd - otkStart);
    CHECK(bobOtkB64.size() == 43, "Bob OTK base64 is 43 chars");
    std::cout << "  Bob IK: " << bobIkB64.substr(0,8) << "...\n";
    std::cout << "  Bob OTK: " << bobOtkB64.substr(0,8) << "...\n";

    // Alice: create outbound session
    std::vector<uint8_t> a_sess_buf(::olm_session_size());
    ::OlmSession* aSess = ::olm_session(a_sess_buf.data());
    rnd = ::olm_create_outbound_session_random_length(aSess);
    rndBuf.resize(rnd);
    fill_random(rndBuf.data(), rnd);
    rc = ::olm_create_outbound_session(aSess, aliceAcc,
        bobIkB64.data(), 43,
        bobOtkB64.data(), 43,
        rndBuf.data(), rnd);
    CHECK(rc != ::olm_error(), "Alice createOutbound");
    if (rc == ::olm_error()) std::cerr << "  err=" << errorStr(aSess) << "\n";

    // Alice: encrypt
    std::string plaintext = "Hello from Alice!";
    rnd = ::olm_encrypt_random_length(aSess);
    rndBuf.resize(rnd);
    fill_random(rndBuf.data(), rnd);
    size_t msgLen = ::olm_encrypt_message_length(aSess, plaintext.size());
    std::vector<uint8_t> msg(msgLen);
    size_t written = ::olm_encrypt(aSess, (void*)plaintext.data(), plaintext.size(),
        rndBuf.data(), rnd, msg.data(), msgLen);
    CHECK(written != ::olm_error(), "Alice encrypt");
    int msgType = ::olm_encrypt_message_type(aSess);
    CHECK_EQ(msgType, 0, "Pre-key message type is 0");
    std::string encMsg((char*)msg.data(), written);

    // Bob: create inbound session from pre-key
    std::vector<uint8_t> b_sess_buf(::olm_session_size());
    ::OlmSession* bSess = ::olm_session(b_sess_buf.data());
    std::vector<uint8_t> tmpMsg(msg);
    rc = ::olm_create_inbound_session(bSess, bobAcc, tmpMsg.data(), written);
    CHECK(rc != ::olm_error(), "Bob createInbound");
    if (rc == ::olm_error()) std::cerr << "  err=" << errorStr(bSess) << "\n";

    // Bob: decrypt — restore message copy
    tmpMsg.resize(written);
    std::memcpy(tmpMsg.data(), msg.data(), written);
    size_t ptLen = ::olm_decrypt_max_plaintext_length(bSess, 0, tmpMsg.data(), written);
    if (ptLen == ::olm_error()) std::cerr << "  decrypt ptLen error: " << errorStr(bSess) << "\n";
    CHECK(ptLen != ::olm_error(), "Bob decrypt ptLen valid");
    std::vector<uint8_t> pt(ptLen);
    tmpMsg.resize(written);
    std::memcpy(tmpMsg.data(), msg.data(), written);
    size_t ptWritten = ::olm_decrypt(bSess, 0, tmpMsg.data(), written, pt.data(), ptLen);
    if (ptWritten == ::olm_error()) std::cerr << "  decrypt error: " << errorStr(bSess) << "\n";
    CHECK(ptWritten != ::olm_error(), "Bob decrypt");
    std::string decrypted((char*)pt.data(), ptWritten);
    CHECK_EQ(decrypted, plaintext, "Plaintext matches!");

    ::olm_clear_session(aSess);
    ::olm_clear_session(bSess);
    ::olm_clear_account(bobAcc);
    ::olm_clear_account(aliceAcc);

    std::cout << "--- test_olm_roundtrip PASSED ---\n";
}

static void test_real_account(const std::string& pickleB64, const std::string& pickleKey,
                               const std::string& bodyB64) {
    std::cout << "\n--- test_real_account ---\n";

    // Step 1: Load real Bob account from pickle
    std::string pickleRaw = progressive::desktop::base64Decode(pickleB64);
    std::cout << "  pickle raw size=" << pickleRaw.size() << " key=" << pickleKey << "\n";

    progressive::OlmAccount bob;
    auto up = bob.unpickle(pickleKey, pickleRaw);
    CHECK(up.success, "Unpickle Bob's real account from session.db");

    auto bobCurve = bob.curve25519Key();
    CHECK(bobCurve.success, "Got Bob's real curve25519");
    std::cout << "  Bob real curve25519: " << bobCurve.data.substr(0, 8) << "...\n";

    // Step 2: Decode body from RAW toDevice log
    std::string bodyRaw = progressive::desktop::base64Decode(bodyB64);
    std::cout << "  body raw size=" << bodyRaw.size() << " first 8 bytes: ";
    for (int i = 0; i < 8 && i < (int)bodyRaw.size(); ++i)
        std::printf("%02x ", (unsigned char)bodyRaw[i]);
    std::cout << "\n";

    // Step 3: createInbound
    progressive::OlmSession bobSession;
    auto in = bobSession.createInbound(bob, bodyRaw);
    CHECK(in.success, "createInbound with real account + real body");

    // Step 4: decrypt
    auto dec = bobSession.decrypt(bodyRaw, 0);
    CHECK(dec.success, "decrypt with real account + real body");
    std::cout << "  decrypted plaintext: " << dec.data.substr(0, 80) << "...\n";

    std::cout << "--- test_real_account PASSED ---\n";
}

int main(int argc, char** argv) {
    std::cout << "=== Olm Inbound Test (raw C API) ===\n\n";

    // Always run the synthetic roundtrip
    test_olm_roundtrip();

    // If pickle + body provided, also test with real account
    if (argc >= 4) {
        std::string pickleB64 = argv[1];
        std::string pickleKey = argv[2];
        std::string bodyB64 = argv[3];
        test_real_account(pickleB64, pickleKey, bodyB64);
    } else {
        std::cout << "\nUsage for real account test:\n";
        std::cout << "  " << argv[0] << " <pickle_b64> <pickle_key> <body_b64>\n";
    }

    if (failures == 0) { std::cout << "\nALL TESTS PASSED\n"; return 0; }
    std::cerr << failures << " FAILURE(S)\n"; return 1;
}
