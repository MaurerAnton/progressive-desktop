#pragma once
#include <QObject>
#include <QPointer>
#include "core/fast_sync.hpp"
#include "../notifications.hpp"

class QLabel;
class QWidget;

namespace progressive::desktop {

class MatrixClient;
class RoomStore;
class MainWindow;

class SyncResponseHandler : public QObject {
    Q_OBJECT
public:
    SyncResponseHandler(MatrixClient* client, RoomStore* roomStore,
                        DesktopNotifier* notifier, QLabel* roomListHeader,
                        QPointer<MainWindow> mw, QObject* parent = nullptr);

    void handle(FastSyncResponse resp);

private:
    MatrixClient* client_;
    RoomStore* roomStore_;
    DesktopNotifier* notifier_;
    QLabel* roomListHeader_;
    QPointer<MainWindow> mw_;
};

} // namespace progressive::desktop
