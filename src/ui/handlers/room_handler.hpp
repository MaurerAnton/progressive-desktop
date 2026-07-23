// src/ui/room_handler.hpp — room interaction extracted from MainWindow.
#pragma once
#include <QObject>
#include <QPointer>
#include <QString>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <fstream>

class QLabel;
class QListView;
class QPushButton;

namespace progressive::desktop {

class MatrixClient;
class RoomStore;
class RoomListModel;
class TimelineModel;
class ImageLoader;
class MessageEdit;
class MainWindow;
class SyncEngine;
class ThreadHandler;
class RoomContextMenu;

class RoomHandler : public QObject {
    Q_OBJECT
public:
    RoomHandler(std::shared_ptr<MatrixClient> client, RoomStore* roomStore,
                RoomListModel* roomModel, TimelineModel* timelineModel,
                SyncEngine* sync, ImageLoader* imageLoader,
                QListView* roomList, QListView* timelineView,
                QLabel* statusLabel, QLabel* timelinePlaceholder,
                QPushButton* loadMoreBtn, QPushButton* chatLogBtn,
                MessageEdit* messageEdit,
                QPointer<MainWindow> mainWindow,
                QObject* parent = nullptr);
    ~RoomHandler();

    void setClient(std::shared_ptr<MatrixClient> c) { client_ = std::move(c); }

    const std::string& currentRoomId() const { return currentRoomIdStr_; }
    const std::string& currentPrevBatch() const { return currentPrevBatch_; }
    const auto& memberAvatarCache() const { return memberAvatarCache_; }
    auto& memberAvatarCache() { return memberAvatarCache_; }

    void clearCurrentRoom() { currentRoomIdStr_.clear(); }
    void setCurrentPrevBatch(const std::string& pb) { currentPrevBatch_ = pb; }
    ThreadHandler* threadHandler() const { return threadHandler_; }

signals:
    void roomSwitchRequested(const QString& roomId);
    void threadOpenRequested(const QString& rootEventId);
    void threadCloseRequested();

public slots:
    void onRoomClicked(const QModelIndex& index);
    void onRoomListContextMenu(const QPoint& pos);
    void onLoadMoreClicked();
    void onTimelineContextMenu(const QPoint& pos);
    void closeThreadView();
    void acceptInvite(const QString& roomId);
    void rejectInvite(const QString& roomId);
    void openThreadView(const QString& rootEventId);

private:
    std::shared_ptr<MatrixClient> client_;
    RoomStore* roomStore_;
    RoomListModel* roomModel_;
    TimelineModel* timelineModel_;
    SyncEngine* sync_;
    ImageLoader* imageLoader_;
    QListView* roomList_;
    QListView* timelineView_;
    QLabel* statusLabel_;
    QLabel* timelinePlaceholder_;
    QPushButton* loadMoreBtn_;
    QPushButton* chatLogBtn_;
    MessageEdit* messageEdit_;
    QPointer<MainWindow> mainWindow_;
    ThreadHandler* threadHandler_;
    RoomContextMenu* contextMenu_;
    std::shared_ptr<bool> roomLifeToken_;

    std::string currentRoomIdStr_;
    std::string currentPrevBatch_;
    std::unordered_map<std::string, std::string> memberAvatarCache_;
    bool chatLogging_ = false;
    std::unique_ptr<std::ofstream> chatLogFile_;
};

} // namespace progressive::desktop
