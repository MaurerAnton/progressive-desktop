#include "session_bootstrap.hpp"
#include "core/matrix_client.hpp"
#include "core/session_store.hpp"
#include "core/sync_engine.hpp"
#include "core/memory_stats.hpp"
#include "core/utils.hpp"
#include "core/debug_log.hpp"
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

void SessionBootstrap::start(const std::shared_ptr<MatrixClient>& client, const std::shared_ptr<SessionStore>& store, SyncEngine* sync,
                      QComboBox* accountCombo, QLabel* userLabel, QLabel* statusLabel,
                      ImageLoader* imageLoader, TimelineDelegate* timelineDelegate,
                      DesktopNotifier* notifier) {
    if (!client || !client->isLoggedIn()) return;

    auto acct = client->account();
    LOG(LogChannel::E2EE, "bootstrap: deviceId=%.20s (empty=%d)",
        acct.deviceId.c_str(), acct.deviceId.empty() ? 1 : 0);
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

    sync->setClient(client);
    sync->setSessionStore(store);
    E2eeInitHandler::init(client.get(), store.get(), sync,
        [=](bool ok, bool keysPublished) {
            LOG(LogChannel::E2EE, "E2eeInit: ok=%d decryptor isInitialized=%d",
                ok ? 1 : 0, sync->decryptor()->isInitialized() ? 1 : 0);
            if (!ok) {
                std::cerr << "[e2ee] init failed — continuing without E2EE\n";
            }
            if (keysPublished) {
                statusLabel->setText("E2EE ready. Refreshing session...");
            } else {
                statusLabel->setText("E2EE keys uploading...");
            }

            if (!client->account().refreshToken.empty()) {
                LOG(LogChannel::E2EE, "pre-refresh: trying /refresh refreshToken len=%zu",
                    client->account().refreshToken.size());
                auto refresh = client->refreshAccessToken(client->account().refreshToken);
                LOG(LogChannel::E2EE, "pre-refresh: httpStatus=%d ok=%d accessToken len=%zu",
                    refresh.httpStatus, refresh.ok ? 1 : 0, refresh.data.accessToken.size());
                if (refresh.httpStatus == 200 && !refresh.data.accessToken.empty()) {
                    AccountInfo acct = client->account();
                    acct.accessToken = refresh.data.accessToken;
                    if (!refresh.data.refreshToken.empty())
                        acct.refreshToken = refresh.data.refreshToken;
                    client->setAccount(acct);
                    client->persistSession();
                    std::fprintf(stderr, "[session] pre-refresh OK — token updated\n");
                } else {
                    std::fprintf(stderr, "[session] pre-refresh FAILED (will try again on 401): %s\n",
                        refresh.error.message.c_str());
                }
            }
            statusLabel->setText("Starting sync...");

            notifier->init();
            logMemorySnapshot("before-first-sync");
            sync->start();
        });
}

} // namespace progressive::desktop
