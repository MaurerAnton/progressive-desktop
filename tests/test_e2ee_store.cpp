#include "core/crypto/megolm_store.hpp"
#include "core/crypto/decryptor.hpp"
#include <iostream>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::cerr << "FAIL: " << msg << " (line " << __LINE__ << ")\n"; failures++; } \
    else { std::cout << "ok: " << msg << "\n"; } \
} while (0)
#define CHECK_EQ(a, b, msg) do { \
    if ((a) != (b)) { std::cerr << "FAIL: " << msg << " (expected " << (b) << " got " << (a) << ") line " << __LINE__ << "\n"; failures++; } \
    else { std::cout << "ok: " << msg << "\n"; } \
} while (0)

static void test_megolm_empty_pickle() {
    progressive::desktop::MegolmStore store;
    std::string pkl = store.pickleAll("empty-key");
    CHECK(pkl == "[]" || pkl.empty(),
        "pickleAll on empty store returns [] or empty");
    CHECK(store.unpickleAll("empty-key", ""),
        "unpickleAll(\"\") returns true (no-op)");
    CHECK(store.unpickleAll("empty-key", "[]"),
        "unpickleAll(\"[]\") returns true (empty array)");
}

static void test_megolm_garbage_unpickle() {
    progressive::desktop::MegolmStore store;
    bool ok = store.unpickleAll("garbage-key", "not valid json at all");
    CHECK(!ok, "unpickleAll with garbage data returns false");
}

static void test_stale_device_cap() {
    progressive::desktop::Decryptor dec;
    dec.init();

    // Insert 1000 entries — should all be stale
    for (int i = 0; i < 1000; i++) {
        dec.markDevicesStale({"@user" + std::to_string(i) + ":matrix.org"});
    }
    CHECK(dec.isDeviceStale("@user999:matrix.org"),
        "user 999 is stale after 1000 inserts");
    CHECK(dec.isDeviceStale("@user0:matrix.org"),
        "user 0 is stale after cap not exceeded");

    // Insert beyond cap — 1001st should NOT be added
    dec.markDevicesStale({"@beyond:matrix.org"});
    CHECK(!dec.isDeviceStale("@beyond:matrix.org"),
        "user beyond cap NOT stale (cap at 1000)");

    // clearStale works
    dec.clearStale("@user0:matrix.org");
    CHECK(!dec.isDeviceStale("@user0:matrix.org"),
        "clearStale removes stale flag");
}


// Export/import roundtrip: outbound session appears in the envelope; a
// hand-built envelope imports into the store.
static void test_key_export_import() {
    progressive::desktop::Decryptor dec;
    CHECK(dec.init(), "xport: decryptor init");

    // Create an outbound session -> appears in the export envelope.
    std::string sessId = dec.getOrCreateOutboundSession("!room1:test");
    CHECK(!sessId.empty(), "xport: outbound session created");
    std::string envelope = dec.exportAllKeys();
    CHECK(!envelope.empty(), "xport: export envelope non-empty");
    CHECK(envelope.find("\"version\":1") != std::string::npos, "xport: version 1");
    CHECK(envelope.find("!room1:test") != std::string::npos, "xport: room in envelope");

    // Full roundtrip: import the first decryptor's own envelope into a second.
    progressive::desktop::Decryptor dec2;
    CHECK(dec2.init(), "xport: decryptor2 init");
    int n = dec2.importKeys(envelope);
    CHECK(n > 0, "xport: import returns count > 0");
    (void)n;
}

int main() {
    test_key_export_import();
    test_megolm_empty_pickle();
    test_megolm_garbage_unpickle();
    test_stale_device_cap();
    if (failures == 0) { std::cout << "\nALL TESTS PASSED\n"; return 0; }
    std::cout << "\n" << failures << " TEST(S) FAILED\n";
    return 1;
}
