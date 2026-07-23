// src/ui/toolbar_handler.hpp — toolbar actions extracted from MainWindow.
#pragma once
#include <QObject>
#include <QAction>
#include <string>
#include <fstream>
#include <memory>

class QLabel;
class QWidget;
class QPushButton;

namespace progressive::desktop {

class MatrixClient;
class RoomListModel;
class RoomStore;
class TimelineModel;
class RoomHandler;

class ToolbarHandler : public QObject {
    Q_OBJECT
public:
    ToolbarHandler(std::shared_ptr<MatrixClient> client, RoomListModel* roomModel,
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
        void setClient(std::shared_ptr<MatrixClient> c) { client_ = std::move(c); }

    void setRoomHandler(RoomHandler* rh) { roomHandler_ = rh; }
    void setInterfaceElements(QPushButton* chatLog, QPushButton* threadBtn) {
        chatLogBtn_ = chatLog; threadBtn_ = threadBtn;
    }

signals:
    void fullscreenToggled();

public slots:
    void doFullscreen();
    void onToggleChatLog();
    void toggleThreadPanel();

private slots:
    void onNewChat();
    void onJoinRoom();
    void onBrowseRooms();
    void onAllThreads();
    void onRoomSettings();
    void onRoomMembers();
    void onSettings();

private:
    std::shared_ptr<MatrixClient> client_;
    RoomListModel* roomModel_;
    RoomStore* roomStore_;
    TimelineModel* timelineModel_;
    QLabel* statusLabel_;
    QWidget* parentWidget_;
    RoomHandler* roomHandler_ = nullptr;
    QPushButton* chatLogBtn_ = nullptr;
    QPushButton* threadBtn_ = nullptr;
    bool chatLogging_ = false;
    bool isFullscreen_ = false;
    std::unique_ptr<std::ofstream> chatLogFile_;

    QAction* fullscreenAction_ = nullptr;
};

} // namespace progressive::desktop
