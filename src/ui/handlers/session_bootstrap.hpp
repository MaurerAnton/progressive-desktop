#pragma once
#include <string>
#include <memory>

class QComboBox;
class QLabel;

namespace progressive::desktop {

class MatrixClient;
class SessionStore;
class SyncEngine;
class ImageLoader;
class TimelineDelegate;
class DesktopNotifier;

class SessionBootstrap {
public:
    static void start(const std::shared_ptr<MatrixClient>& client, const std::shared_ptr<SessionStore>& store, SyncEngine* sync,
                      QComboBox* accountCombo, QLabel* userLabel, QLabel* statusLabel,
                      ImageLoader* imageLoader, TimelineDelegate* timelineDelegate,
                      DesktopNotifier* notifier);
};

} // namespace progressive::desktop
