// src/main.cpp — Phase 1 smoke test.
//
// Verifies that the desktop HTTP client, MatrixClient, SessionStore, and
// SyncEngine all link and run. NOT a real UI — Phase 2 adds Qt6 widgets.
//
// Run modes:
//   progressive-desktop                    # GUI smoke window
//   progressive-desktop --smoke            # quick link test, exit
//   progressive-desktop --discover URL     # discover homeserver, print result
//   progressive-desktop --login USER PASS  # login flow test
//   progressive-desktop --sync N            # do N syncs then exit (headless)

#include <QApplication>
#include <QLabel>
#include <QWidget>
#include <QVBoxLayout>

#include <curl/curl.h>

#include <iostream>
#include <string>
#include <thread>
#include <chrono>

#include "core/http_client.hpp"
#include "core/matrix_client.hpp"
#include "core/session_store.hpp"
#include "core/sync_engine.hpp"

#include <progressive/markdown.hpp>

using namespace progressive::desktop;

static int smoke() {
    std::cout << "=== progressive-desktop Phase 1 smoke test ===\n";
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
    std::cout << "All Phase 1 components linked successfully.\n";
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
    // Persist and reload
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
    se.onSync([&](const progressive::SyncResponse& r) {
        syncs_seen++;
        std::cout << "  sync #" << syncs_seen
                  << ": join=" << r.rooms.join.size()
                  << " invite=" << r.rooms.invite.size()
                  << " leave=" << r.rooms.leave.size()
                  << " toDevice=" << r.toDevice.events.size() << "\n";
    });
    se.onStateChange([](SyncEngineState st, const SyncEngineStats& stats) {
        std::cout << "  state: " << static_cast<int>(st)
                  << " rooms=" << stats.roomsJoined
                  << " events=" << stats.timelineEvents
                  << " errors=" << stats.errors << "\n";
    });
    se.start();

    // Wait until we've seen `count` syncs (or timeout)
    for (int i = 0; i < 60 && syncs_seen < count; ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    se.stop();
    std::cout << "done: " << syncs_seen << " syncs received\n";
    return syncs_seen > 0 ? 0 : 1;
}

int main(int argc, char** argv) {
    // CLI subcommands first (no Qt needed for those)
    if (argc >= 2) {
        std::string cmd = argv[1];
        if (cmd == "--smoke")    return smoke();
        if (cmd == "--discover" && argc >= 3) return discover(argv[2]);
        if (cmd == "--login"    && argc >= 4) return loginTest(argv[2], argv[3]);
        if (cmd == "--sync"     && argc >= 3) return syncTest(std::stoi(argv[2]));
    }

    // Default: Qt6 window
    QApplication app(argc, argv);
    QApplication::setApplicationName("progressive-desktop");
    QApplication::setApplicationVersion("0.0.1");

    QWidget window;
    window.setWindowTitle("Progressive Chat — Desktop (Phase 1)");
    window.resize(900, 600);
    auto* layout = new QVBoxLayout(&window);

    auto* title = new QLabel("<h2>Progressive Chat — Desktop</h2>"
                             "<p>Phase 1 — core plumbing ready</p>");
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    auto* probe = new QLabel(QString("progressive_native: linked.\n"
                                     "Markdown probe: %1\n"
                                     "HTTP/libcurl: %2")
        .arg(QString::fromStdString(progressive::markdownToHtml("**hi**")))
        .arg(curl_version()));
    probe->setWordWrap(true);
    probe->setAlignment(Qt::AlignCenter);
    layout->addWidget(probe);

    auto* status = new QLabel("Run from CLI for Matrix tests:\n"
                              "  progressive-desktop --smoke\n"
                              "  progressive-desktop --discover matrix.org\n"
                              "  progressive-desktop --login user pass\n"
                              "  progressive-desktop --sync 3");
    status->setAlignment(Qt::AlignCenter);
    layout->addWidget(status);

    window.show();
    return app.exec();
}
