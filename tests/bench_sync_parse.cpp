// tests/bench_sync_parse.cpp — benchmark progressive_native's slow JSON parser
// vs the desktop fast_sync.cpp simdjson-based parser.
//
// Usage:
//   test_phase3 --record          # record a real /sync response from matrix.org
//                                 # requires saved session (--login first)
//                                 # saves to tests/fixtures/sync_5mb.json (gitignored)
//
//   test_phase3                   # benchmark: parse the fixture N times
//                                 # with both parsers, report stats
//
// Build:  cmake --build build -j4
// Run:    ./build/test_phase3 --record
//         ./build/test_phase3

#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <sys/resource.h>

#include "core/http_client.hpp"
#include "core/matrix_client.hpp"
#include "core/session_store.hpp"
#include "core/fast_sync.hpp"

#include <progressive/sync_models.hpp>

using namespace progressive::desktop;

namespace {

constexpr const char* FIXTURE_PATH = "tests/fixtures/sync_5mb.json";
constexpr int BENCH_ITERS = 10;

long getRssKb() {
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return ru.ru_maxrss;  // kilobytes on Linux
}

std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void writeFile(const std::string& path, const std::string& content) {
    std::ofstream f(path, std::ios::binary);
    f.write(content.data(), static_cast<std::streamsize>(content.size()));
}

int record() {
    SessionStore s;
    s.open("/tmp/progressive-desktop-test.db");
    auto acct_opt = s.loadAccount();
    if (!acct_opt) {
        std::cerr << "no saved session. Run: ./build/progressive-desktop --login user pass\n";
        return 1;
    }
    MatrixClient c;
    c.setAccount(*acct_opt);

    std::cout << "doing initial /sync (no since token, full state)...\n";
    auto r = c.sync("", 30000);
    if (!r.ok) {
        std::cerr << "sync failed: " << r.error.message << "\n";
        return 1;
    }

    // Re-fetch as raw body (we want the raw JSON, not the parsed struct).
    std::ostringstream url;
    url << acct_opt->homeserverUrl << "/_matrix/client/v3/sync?timeout=30000&full_state=false";
    auto resp = httpGet(url.str(), {{"Authorization", "Bearer " + acct_opt->accessToken},
                                     {"Accept", "application/json"}}, 60000);
    if (!resp.success) {
        std::cerr << "raw sync failed: " << resp.errorMessage << "\n";
        return 1;
    }

    std::string fixturePath = FIXTURE_PATH;
    // Try to create tests/fixtures dir
    std::string dir = fixturePath.substr(0, fixturePath.find_last_of('/'));
    if (!dir.empty()) {
        std::string cmd = "mkdir -p " + dir;
        std::system(cmd.c_str());
    }
    writeFile(fixturePath, resp.body);
    std::cout << "saved " << resp.body.size() << " bytes to " << fixturePath << "\n";
    return 0;
}

int bench() {
    std::string json = readFile(FIXTURE_PATH);
    if (json.empty()) {
        std::cerr << "fixture not found at " << FIXTURE_PATH
                  << "\nrun: ./build/test_phase3 --record\n";
        return 1;
    }

    std::cout << "fixture: " << json.size() << " bytes\n\n";

    // --- Slow path: progressive_native parser ---
    long rssBefore = getRssKb();
    std::vector<std::chrono::microseconds> slowTimes;
    slowTimes.reserve(BENCH_ITERS);
    for (int i = 0; i < BENCH_ITERS; ++i) {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto resp = progressive::parseSyncResponse(json);
        auto t1 = std::chrono::high_resolution_clock::now();
        slowTimes.push_back(std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0));
        if (i == 0) {
            std::cout << "  slow path: parsed " << resp.rooms.join.size() << " joined rooms\n";
        }
    }
    long rssAfterSlow = getRssKb();

    // --- Fast path: simdjson ---
    std::vector<std::chrono::microseconds> fastTimes;
    fastTimes.reserve(BENCH_ITERS);
    for (int i = 0; i < BENCH_ITERS; ++i) {
        auto t0 = std::chrono::high_resolution_clock::now();
        std::string err;
        auto resp = parseSyncResponseFast(json, err);
        auto t1 = std::chrono::high_resolution_clock::now();
        fastTimes.push_back(std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0));
        if (!err.empty()) {
            std::cerr << "fast path error: " << err << "\n";
            return 1;
        }
        if (i == 0) {
            std::cout << "  fast path: parsed " << resp.joinedRooms.size() << " joined rooms\n";
        }
    }
    long rssAfterFast = getRssKb();

    // --- Stats ---
    auto medianUs = [](const std::vector<std::chrono::microseconds>& v) -> int64_t {
        std::vector<int64_t> xs;
        for (auto t : v) xs.push_back(t.count());
        std::sort(xs.begin(), xs.end());
        return xs[xs.size() / 2];
    };

    int64_t slowMed = medianUs(slowTimes);
    int64_t fastMed = medianUs(fastTimes);

    std::cout << "\n=== Benchmark results (" << BENCH_ITERS << " iters each) ===\n";
    std::cout << "  progressive_native parser (slow):  median " << slowMed << " us";
    std::cout << "  (RSS " << (rssAfterSlow - rssBefore) << " KB delta)\n";
    std::cout << "  simdjson fast path:                 median " << fastMed << " us";
    std::cout << "  (RSS " << (rssAfterFast - rssAfterSlow) << " KB delta)\n";
    std::cout << "  speedup: " << (slowMed / std::max<int64_t>(1, fastMed)) << "x\n";

    if (fastMed < slowMed) {
        std::cout << "\n✓ PASS — fast path is faster\n";
        return 0;
    }
    std::cout << "\n✗ FAIL — fast path is slower (unexpected)\n";
    return 1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 2 && std::string(argv[1]) == "--record") {
        return record();
    }
    return bench();
}
