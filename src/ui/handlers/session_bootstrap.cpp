#include "session_bootstrap.hpp"
#include "core/matrix_client.hpp"
#include "core/session_store.hpp"
#include "core/sync_engine.hpp"
#include "core/memory_stats.hpp"
#include "core/utils.hpp"
#include "../dialogs/prefs_dialog.hpp"
#include "../shared/image_loader.hpp"
#include "../timeline/timeline_delegate.hpp"
#include "e2ee_init_handler.hpp"
#include "../notifications.hpp"
#include <QComboBox>
#include <QLabel>
#include <cstdio>
#include <iostream>

namespace progressive::desktop {

void SessionBootstrap::start(MatrixClient* client, SessionStore* store, SyncEngine* sync,
                      QComboBox* accountCombo, QLabel* userLabel, QLabel* statusLabel,
                      ImageLoader* imageLoader, TimelineDelegate* timelineDelegate,
                      DesktopNotifier* notifier) {
    if (!client || !client->isLoggedIn()) return;

    auto acct = client->account();
    if (acct.deviceId.empty() || acct.deviceId == "PROGRESSIVE_DESKTOP") {
        acct.deviceId = generateUUID();
        client->setAccount(acct);
        client->persistSession();
        std::fprintf(stderr, "[session] generated device_id: %s\n", acct.deviceId.c_str());
    }

    std::fprintf(stderr, "[session] loaded: user=%s device=%s homeserver=%s token_prefix=%s refresh=%s\n",
                 acct.userId.c_str(), acct.deviceId.c_str(), acct.homeserverUrl.c_str(),
                 acct.accessToken.substr(0, 8).c_str(),
                 acct.refreshToken.empty() ? "(none)" : (acct.refreshToken.substr(0, 8) + "...").c_str());

    imageLoader->setClient(client);
    timelineDelegate->setMyUserId(client->account().userId);
    if (store) {
        auto accounts = store->listAccounts();
        for (const auto& a : accounts) {
            QString label = QString::fromStdString(a.userId);
            accountCombo->addItem(label);
            if (a.userId == client->account().userId)
                accountCombo->setCurrentIndex(accountCombo->count() - 1);
        }
    }
    int cacheSize = PrefsDialog::imageCacheSize();
    imageLoader->setCacheSize(cacheSize);
    std::fprintf(stderr, "[mem] image cache size: %d\n", cacheSize);
    userLabel->setText(" " + QString::fromStdString(client->account().userId) + " ");
    statusLabel->setText("Starting sync...");

    E2eeInitHandler::init(client, store, sync,
        [=](bool ok, bool keysPublished) {
            if (!ok) {
                std::cerr << "[e2ee] init failed — continuing without E2EE\n";
            }
            if (keysPublished) {
                statusLabel->setText("E2EE ready. Starting sync...");
            } else {
                statusLabel->setText("E2EE keys uploading...");
            }
            notifier->init();
            sync->setClient(client);
            sync->setSessionStore(store);
            logMemorySnapshot("before-first-sync");
            sync->start();
        });
}

} // namespace progressive::desktop
