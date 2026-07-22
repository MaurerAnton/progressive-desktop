// src/ui/e2ee_init_handler.hpp
#pragma once
#include <string>
#include <functional>

namespace progressive::desktop {

class MatrixClient;
class SessionStore;
class SyncEngine;

class E2eeInitHandler {
public:
    static void init(MatrixClient* client, SessionStore* store,
                     SyncEngine* sync,
                     std::function<void(bool ok, bool keysPublished)> callback);

    static void persistCrypto(MatrixClient* client, SessionStore* store,
                              SyncEngine* sync);
};

} // namespace progressive::desktop
