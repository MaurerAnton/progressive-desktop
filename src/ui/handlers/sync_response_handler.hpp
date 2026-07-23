// src/ui/handlers/sync_response_handler.hpp
#pragma once
#include <QObject>
#include <QPointer>
#include <memory>
#include "core/fast_sync.hpp"
#include "../notifications.hpp"
#include "../room/room_store.hpp"

class QLabel;
class QPushButton;
class QWidget;

namespace progressive::desktop {

class MatrixClient;
class RoomStore;
class RoomListModel;
class TimelineModel;
class RoomHandler;

class SyncResponseHandler : public QObject {
    Q_OBJECT
public:
    SyncResponseHandler(MatrixClient* client, RoomStore* roomStore,
                        RoomListModel* roomModel, TimelineModel* timelineModel,
                        DesktopNotifier* notifier, QLabel* roomListHeader,
                        QLabel* inviteHeader, QLabel* statusLabel,
                        QWidget* placeholder, QWidget* timelineView,
                        QWidget* messageEdit, QPushButton* loadMoreBtn,
                        RoomHandler* roomHandler, QObject* parent = nullptr);
    ~SyncResponseHandler();

    void setClient(MatrixClient* c) { client_ = c; }

    void handle(FastSyncResponse resp);

private:
    MatrixClient* client_;
    RoomStore* roomStore_;
    RoomListModel* roomModel_;
    TimelineModel* timelineModel_;
    DesktopNotifier* notifier_;
    QLabel* roomListHeader_;
    QLabel* inviteHeader_;
    QLabel* statusLabel_;
    QWidget* placeholder_;
    QWidget* timelineView_;
    QWidget* messageEdit_;
    QPushButton* loadMoreBtn_;
    RoomHandler* roomHandler_;
    LifeToken syncLifeToken_;
};

} // namespace progressive::desktop
