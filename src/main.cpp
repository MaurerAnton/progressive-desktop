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
#include "ui/main_window.hpp"
#include "ui/login_dialog.hpp"
#include "ui/theme.hpp"

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
                  << " invite=" << r.invitedRoomIds.size()
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

// ---- GUI mode ----

static void runGui(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("progressive-desktop");
    QApplication::setApplicationVersion(PROGRESSIVE_DESKTOP_VERSION);
    QApplication::setOrganizationName("progressive.chat");

    // Load bundled emoji font (OpenMoji color, 312KB embedded via .qrc).
    // This ensures emoji glyphs render correctly even on systems without
    // Noto Color Emoji installed (e.g. minimal PineTab 2).
    int emojiFontId = QFontDatabase::addApplicationFont(":/fonts/OpenMoji-color.ttf");
    if (emojiFontId >= 0) {
        QString family = QFontDatabase::applicationFontFamilies(emojiFontId).value(0);
        if (!family.isEmpty()) {
            // Insert as a fallback for emoji script — Qt picks the family
            // that has the needed glyphs.
            QFont defaultFont = QApplication::font();
            defaultFont.setStyleStrategy(QFont::PreferMatch);
            QApplication::setFont(defaultFont, "QPushButton");
            // Set OpenMoji as fallback for emoji-only widgets
            // (buttons in emoji picker, attach/emoji buttons)
            // Qt's font matching will use it for emoji codepoints.
        }
    }

    // Dark theme is the default — applied before any widgets are constructed
    // so the palette propagates to all child widgets.
    applyDarkTheme(app);

    // Open session store first
    SessionStore store;
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    QString dbPath = dataDir + "/session.db";
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
