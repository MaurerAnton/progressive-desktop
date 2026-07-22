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
class QListView;

class ToolbarHandler : public QObject {
    Q_OBJECT
public:
    ToolbarHandler(MatrixClient* client, RoomListModel* roomModel,
                   RoomStore* roomStore, TimelineModel* timelineModel,
                   QLabel* statusLabel, QWidget* parent);

    QAction* newChatAction() const { return newChatAction_; }
    QAction* joinRoomAction() const { return joinRoomAction_; }
    QAction* browseRoomsAction() const { return browseRoomsAction_; }
    QAction* allThreadsAction() const { return allThreadsAction_; }
    QAction* roomSettingsAction() const { return roomSettingsAction_; }
    QAction* roomMembersAction() const { return roomMembersAction_; }
    QAction* settingsAction() const { return settingsAction_; }
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

    QAction* newChatAction_ = nullptr;
    QAction* joinRoomAction_ = nullptr;
    QAction* browseRoomsAction_ = nullptr;
    QAction* allThreadsAction_ = nullptr;
    QAction* roomSettingsAction_ = nullptr;
    QAction* roomMembersAction_ = nullptr;
    QAction* settingsAction_ = nullptr;
    QAction* fullscreenAction_ = nullptr;
};

} // namespace progressive::desktop
