// src/ui/e2ee_init_handler.cpp
#include "e2ee_init_handler.hpp"
#include "core/matrix_client.hpp"
#include "core/session_store.hpp"
#include "core/sync_engine.hpp"
#include "core/crypto/decryptor.hpp"
#include "core/debug_log.hpp"

#include <iostream>
#include "core/thread_pool.hpp"

namespace progressive::desktop {

void E2eeInitHandler::init(MatrixClient* client, SessionStore* store,
                             SyncEngine* sync,
                             std::function<void(bool ok, bool keysPublished)> callback) {
    if (!client || !sync) {
        if (callback) callback(false, false);
        return;
    }

    auto acct = client->account();
    std::string pickleKey = acct.userId + "/" + acct.deviceId;
    bool e2eeOk = false;
    bool keysPublished = false;

    try {
        std::string savedPickle;
        std::string savedKey;
        bool savedShared = false;
        int savedKeyCount = 0;
        if (store) {
            auto saved = store->loadOlmAccount(pickleKey);
            if (saved) {
                savedPickle = saved->pickle;
                savedKey = saved->pickleKey;
                savedShared = saved->shared;
                savedKeyCount = saved->uploadedKeyCount;
            }
        }

        if (!savedPickle.empty()) {
            e2eeOk = sync->decryptor()->init(savedPickle, savedKey, savedShared);
            if (e2eeOk) {
                sync->decryptor()->account()->setUploadedKeyCount(savedKeyCount);
            } else {
                std::cerr << "[e2ee] failed to load saved olm account — creating new one\n";
                e2eeOk = sync->decryptor()->init();
            }
        } else {
            e2eeOk = sync->decryptor()->init();
        }

        if (!e2eeOk) {
            std::cerr << "[e2ee] failed to create olm account — continuing without E2EE\n";
        } else {
            auto keys = sync->decryptor()->identityKeys();
            std::cerr << "[e2ee] olm account ready: curve25519=" << keys.curve25519
                      << " ed25519=" << keys.ed25519 << "\n";

            sync->decryptor()->setCryptoContext(acct.userId, acct.deviceId,
                                                  acct.homeserverUrl, acct.accessToken);

            std::string newPickle = sync->decryptor()->saveAccountPickle(pickleKey);
            if (!newPickle.empty() && store) {
                store->saveOlmAccount(newPickle, pickleKey,
                                      sync->decryptor()->accountShared(),
                                      sync->decryptor()->account()->uploadedKeyCount());
            }

            if (store) {
                auto megolmData = store->loadMegolmSessions(pickleKey);
                if (megolmData && !megolmData->empty()) {
                    sync->decryptor()->megolm()->unpickleAll(pickleKey, *megolmData);
                    std::cerr << "[e2ee] loaded megolm sessions: "
                              << sync->decryptor()->megolm()->sessionCount() << "\n";
                }
                auto outboundData = store->loadOutboundSessions(pickleKey);
                if (outboundData) {
                    sync->decryptor()->unpickleOutboundSessions(pickleKey, *outboundData);
                }
                auto olmSessionsData = store->loadOlmSessions(pickleKey);
                if (olmSessionsData && !olmSessionsData->empty()) {
                    sync->decryptor()->unpickleOlmSessions(pickleKey, *olmSessionsData);
                    std::cerr << "[e2ee] loaded olm sessions: "
                              << sync->decryptor()->olmSessionCount() << "\n";
                    if (olmSessionsData->size() > 500000) {
                        LOG(LogChannel::E2EE, "cleanup: olm sessions pickle is %zu bytes (>500KB) — "
                            "saving trimmed version", olmSessionsData->size());
                        std::string trimmed = sync->decryptor()->pickleOlmSessions(pickleKey);
                        store->saveOlmSessions(trimmed, pickleKey);
                        LOG(LogChannel::E2EE, "cleanup: re-saved %zu olm sessions",
                            sync->decryptor()->olmSessionCount());
                    }
                }
            }

            LOG(LogChannel::E2EE, "E2eeInit: scheduling device keys upload");
            keysPublished = true;
            ThreadPool::instance().enqueue([sync]() {
                sync->uploadDeviceKeys();
            });
        }
    } catch (const std::exception& e) {
        std::cerr << "[e2ee] init failed: " << e.what() << "\n";
    }

    if (callback) callback(e2eeOk, keysPublished);
}

void E2eeInitHandler::persistCrypto(MatrixClient* client, SessionStore* store,
                                     SyncEngine* sync) {
    if (!client || !store || !sync) return;
    if (!sync->decryptor() || !sync->decryptor()->isInitialized()) return;

    std::string pickleKey = client->account().userId + "/" + client->account().deviceId;
    auto megolmPickle = sync->decryptor()->megolm()->pickleAll(pickleKey);
        if (!megolmPickle.empty()) {
            store->saveMegolmSessions(megolmPickle, pickleKey);
            std::fprintf(stderr, "[e2ee] persistCrypto: saved %d megolm sessions\n",
                sync->decryptor()->megolm()->sessionCount());
        }
        auto outboundPickle = sync->decryptor()->pickleOutboundSessions(pickleKey);
        if (!outboundPickle.empty() && outboundPickle != "[]") {
            store->saveOutboundSessions(outboundPickle, pickleKey);
            LOG(LogChannel::E2EE, "persistCrypto: saved outbound sessions");
        }
        auto olmSessionsPickle = sync->decryptor()->pickleOlmSessions(pickleKey);
    std::fprintf(stderr, "[e2ee] persistCrypto: saving %zu olm sessions\n",
                 sync->decryptor()->olmSessionCount());
    if (!olmSessionsPickle.empty()) store->saveOlmSessions(olmSessionsPickle, pickleKey);
}

} // namespace progressive::desktop
