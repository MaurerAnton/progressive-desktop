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

int main() {
    test_megolm_empty_pickle();
    test_megolm_garbage_unpickle();
    test_stale_device_cap();
    if (failures == 0) { std::cout << "\nALL TESTS PASSED\n"; return 0; }
    std::cout << "\n" << failures << " TEST(S) FAILED\n";
    return 1;
}
