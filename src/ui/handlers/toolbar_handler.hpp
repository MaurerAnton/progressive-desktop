// src/ui/toolbar_handler.hpp — toolbar actions extracted from MainWindow.
#pragma once
#include <QObject>
#include <QAction>

class QLabel;
class QWidget;

namespace progressive::desktop {

class MatrixClient;
class RoomListModel;
class RoomStore;
class TimelineModel;

class ToolbarHandler : public QObject {
    Q_OBJECT
public:
    ToolbarHandler(MatrixClient* client, RoomListModel* roomModel,
                   RoomStore* roomStore, TimelineModel* timelineModel,
                   QLabel* statusLabel, QWidget* parent);

    QAction* createNewChatAction();
    QAction* createJoinRoomAction();
    QAction* createBrowseRoomsAction();
    QAction* createAllThreadsAction();
    QAction* createRoomSettingsAction();
    QAction* createRoomMembersAction();
    QAction* createSettingsAction();
    QAction* createFullscreenAction();
    QAction* fullscreenAction() const { return fullscreenAction_; }

signals:
    void fullscreenToggled();

private slots:
    void onNewChat();
    void onJoinRoom();
    void onBrowseRooms();
    void onAllThreads();
    void onRoomSettings();
    void onRoomMembers();
    void onSettings();

private:
    MatrixClient* client_;
    RoomListModel* roomModel_;
    RoomStore* roomStore_;
    TimelineModel* timelineModel_;
    QLabel* statusLabel_;
    QWidget* parentWidget_;

    QAction* fullscreenAction_ = nullptr;
};

} // namespace progressive::desktop
