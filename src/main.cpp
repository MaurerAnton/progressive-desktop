// src/main.cpp — Phase 2 entry point.
//
// Three modes:
//   1. CLI subcommands (--smoke --discover --login --sync) — for testing
//   2. No saved session → MainWindow + LoginDialog
//   3. Saved session → MainWindow with sync already running

#include <QApplication>
#include <QStandardPaths>
#include <QDir>
#include <QTimer>
#include <QFontDatabase>
#include <QFont>

#include <curl/curl.h>

#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <csignal>
#include <cstdlib>

#include "core/http_client.hpp"
#include "core/matrix_client.hpp"
#include "core/session_store.hpp"
#include "core/sync_engine.hpp"
#include "core/version.h"
#include "core/memory_stats.hpp"
#include "ui/main_window.hpp"
#include "ui/dialogs/login_dialog.hpp"
#include "ui/shared/theme.hpp"

#include <simdjson.h>
#include <progressive/markdown.hpp>

using namespace progressive::desktop;

// ---- SIGSEGV handler — prints backtrace on crash ----
static void crashHandler(int sig) {
    const char* msg = "\n*** FATAL: progressive-desktop crashed (signal ";
    std::fwrite(msg, 1, std::strlen(msg), stderr);
    std::fprintf(stderr, "%d) ***\n", sig);
    std::fprintf(stderr, "Please report this with the steps to reproduce.\n");
    std::fflush(stderr);
    std::_Exit(128 + sig);
}

// ---- CLI test subcommands (kept for Phase 1 compat) ----

static int smoke() {
    std::cout << "=== progressive-desktop Phase 2 smoke test ===\n";
    std::cout << "  http_client   : OK (libcurl " << curl_version() << ")\n";
    MatrixClient c;
    std::cout << "  matrix_client  : OK (constructed)\n";
    SessionStore s;
    bool ok = s.open(":memory:");
    std::cout << "  session_store  : " << (ok ? "OK (opened :memory:)" : "FAIL") << "\n";
    SyncEngine se;
    se.setClient(&c);
    se.setSessionStore(&s);
    std::cout << "  sync_engine    : OK (constructed)\n";
    std::cout << "  progressive_native::markdownToHtml(\"**hi**\") = "
              << progressive::markdownToHtml("**hi**") << "\n";
    std::cout << "All Phase 2 components linked successfully.\n";
    return 0;
}

static int discover(const std::string& url) {
    MatrixClient c;
    auto r = c.discoverHomeserver(url);
    if (!r.ok) {
        std::cerr << "discover failed: " << r.error.message << "\n";
        return 1;
    }
    std::cout << "discovered homeserver: " << r.data << "\n";
    c.setAccount({"", "", r.data, "", ""});
    auto v = c.getVersions();
    if (!v.ok) {
        std::cerr << "versions failed: " << v.error.message << "\n";
        return 1;
    }
    std::cout << "versions: " << v.data << "\n";
    auto f = c.getLoginFlows();
    if (!f.ok) {
        std::cerr << "login flows failed: " << f.error.message << "\n";
        return 1;
    }
    std::cout << "login flows: ok (got " << f.data.flows.size() << " flows)\n";
    return 0;
}

static int loginTest(const std::string& user, const std::string& pass) {
    MatrixClient c;
    auto d = c.discoverHomeserver("matrix.org");
    if (!d.ok) { std::cerr << "discover: " << d.error.message << "\n"; return 1; }
    c.setAccount({"", "", d.data, "", ""});
    auto r = c.loginWithPassword(user, pass);
    if (!r.ok) {
        std::cerr << "login failed: " << r.error.code << " — " << r.error.message << "\n";
        return 1;
    }
    std::cout << "logged in as " << r.data.userId
              << " (device " << r.data.deviceId << ")\n";
    SessionStore s;
    s.open("/tmp/progressive-desktop-test.db");
    c.setSessionStore(&s);
    c.persistSession();
    std::cout << "session saved\n";
    return 0;
}

static int syncTest(int count) {
    SessionStore s;
    s.open("/tmp/progressive-desktop-test.db");
    auto acct_opt = s.loadAccount();
    if (!acct_opt) { std::cerr << "no saved session — run --login first\n"; return 1; }
    auto acct = *acct_opt;
    MatrixClient c;
    c.setAccount(acct);
    c.setSessionStore(&s);

    SyncEngine se;
    se.setClient(&c);
    se.setSessionStore(&s);
    int syncs_seen = 0;
    se.onSync([&](const FastSyncResponse& r) {
        syncs_seen++;
        std::cout << "  sync #" << syncs_seen
                  << ": join=" << r.joinedRooms.size()
                   << " invite=" << r.invitedRooms.size()
                  << " leave=" << r.leftRoomIds.size()
                  << " toDevice=" << r.toDeviceEvents
                  << " (" << r.buffer->size() << " bytes)\n";
    });
    se.onStateChange([](SyncEngineState st, const SyncEngineStats& stats) {
        std::cout << "  state: " << static_cast<int>(st)
                  << " rooms=" << stats.roomsJoined
                  << " events=" << stats.timelineEvents
                  << " errors=" << stats.errors << "\n";
    });
    se.start();

    for (int i = 0; i < 60 && syncs_seen < count; ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    se.stop();
    std::cout << "done: " << syncs_seen << " syncs received\n";
    return syncs_seen > 0 ? 0 : 1;
}

// ---- Memory diagnostics ----

static int memcheck() {
    logStructSizes();
    logMemorySnapshot("before-anything");

    // Simulate loading session + creating client
    SessionStore s;
    s.open(":memory:");
    logMemorySnapshot("after-sqlite-open");

    MatrixClient c;
    logMemorySnapshot("after-matrix-client");

    // Simulate a sync parse — create FastSyncResponse with some rooms
    {
        FastSyncResponse resp;
        auto buf = std::make_shared<std::string>(1024 * 1024, 'x');  // 1MB
        auto parser = std::make_shared<simdjson::dom::parser>();
        resp.buffer = buf;
        resp.parser = parser;
        resp.ownedContentStrings = std::make_shared<std::deque<std::string>>();
        for (int i = 0; i < 100; ++i) {
            FastRoom room;
            room.isEncrypted = (i % 3 == 0);
            for (int j = 0; j < 10; ++j) {
                room.timeline.events.push_back(FastEvent{});
                room.stateEvents.push_back(FastEvent{});
            }
            resp.joinedRooms.push_back({"", std::move(room)});
        }
        resp.totalTimelineEvents = 1000;
        resp.toDeviceEvents = 50;
        logMemorySnapshot("after-simulated-sync");
        std::cout << "  simulated " << resp.joinedRooms.size()
                  << " rooms, " << resp.totalTimelineEvents << " events\n";
    }
    logMemorySnapshot("after-freesync-free");

    // Test trim
    int trimOk = trimMemory();
    logMemorySnapshot("after-malloc_trim");
    std::cout << "  malloc_trim returned " << trimOk << " (1=released to OS)\n";

    return 0;
}

// ---- GUI mode ----

static void runGui(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("progressive-desktop");
    QApplication::setApplicationVersion(PROGRESSIVE_DESKTOP_VERSION);
    QApplication::setOrganizationName("progressive.chat");

    // Find the best available emoji font on the system.
    // Noto Color Emoji (COLRv0) works everywhere. OpenMoji/Twemoji (COLRv1)
    // requires FreeType >= 2.13 which may not be available on older systems.
    // Log all emoji candidates found for diagnostics.
    {
        QStringList allFonts = QFontDatabase::families();
        bool found = false;
        for (const QString& name : {"Noto Color Emoji", "Apple Color Emoji",
                                      "Segoe UI Emoji", "Twemoji Mozilla",
                                      "OpenMoji Color", "EmojiOne"}) {
            if (allFonts.contains(name, Qt::CaseInsensitive)) {
                std::fprintf(stderr, "[font] system emoji font found: %s\n", name.toUtf8().data());
                if (!found) {
                    QFont::insertSubstitution(QApplication::font().family(), name);
                    found = true;
                }
            }
        }
        if (!found) {
            std::fprintf(stderr, "[font] WARNING: no emoji font found!\n"
                                 "  Install 'noto-fonts-emoji' on Arch/Manjaro:\n"
                                 "    sudo pacman -S noto-fonts-emoji\n");
        }
    }

    // Dark theme is the default — applied before any widgets are constructed
    // so the palette propagates to all child widgets.
    applyDarkTheme(app);
    Design::fontScale = QApplication::font().pointSize() / 10.0;

    // Open session store first
    SessionStore store;
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    QString dbPath = dataDir + "/session.db";
    std::fprintf(stderr, "[session] data dir: %s\n", dataDir.toUtf8().data());
    store.open(dbPath.toStdString());

    // Create client + load saved session if present
    MatrixClient client;
    client.setSessionStore(&store);
    bool hasSession = client.loadSavedSession();

    MainWindow w;
    w.setClient(&client);
    w.setSessionStore(&store);
    // MainWindow owns the SyncEngine and wires its callbacks in its constructor.

    if (hasSession && client.isLoggedIn()) {
        w.show();
        // Start sync on next event loop tick so the window shows first
        QTimer::singleShot(0, &w, [&]() { w.startWithSavedSession(); });
    } else {
        // Show login dialog
        LoginDialog dlg(&client, &store, &w);
        if (dlg.exec() == QDialog::Accepted && dlg.loggedIn()) {
            w.show();
            QTimer::singleShot(0, &w, [&]() { w.startWithSavedSession(); });
        } else {
            return;  // user cancelled
        }
    }

    app.exec();
}

int main(int argc, char** argv) {
    // Install crash handler so we get a message instead of silent segfault
    std::signal(SIGSEGV, crashHandler);
    std::signal(SIGABRT, crashHandler);
    std::signal(SIGBUS, crashHandler);

    // Initialize libcurl once for the whole process
    progressive::desktop::httpInit();

    if (argc >= 2) {
        std::string cmd = argv[1];
        if (cmd == "--smoke")    { int r = smoke(); httpCleanup(); return r; }
        if (cmd == "--discover" && argc >= 3) { int r = discover(argv[2]); httpCleanup(); return r; }
        if (cmd == "--login"    && argc >= 4) { int r = loginTest(argv[2], argv[3]); httpCleanup(); return r; }
        if (cmd == "--sync"     && argc >= 3) { int r = syncTest(std::stoi(argv[2])); httpCleanup(); return r; }
        if (cmd == "--memcheck") { int r = memcheck(); httpCleanup(); return r; }
    }

    int rc = 0;
    try {
        runGui(argc, argv);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FATAL: unhandled exception: %s\n", e.what());
        rc = 1;
    } catch (...) {
        std::fprintf(stderr, "FATAL: unknown exception\n");
        rc = 1;
    }
    httpCleanup();
    return rc;
}
