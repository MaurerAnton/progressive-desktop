// src/ui/handlers/room_context_menu.hpp — room context menu management.
#pragma once
#include <QObject>
#include <QPointer>
#include <QString>
#include <string>

class QListView;
class QPoint;
class QLabel;

namespace progressive::desktop {

class MatrixClient;
class TimelineModel;
class RoomListModel;
class ThreadHandler;
class MainWindow;

class RoomContextMenu : public QObject {
    Q_OBJECT
public:
    RoomContextMenu(std::shared_ptr<MatrixClient> client, TimelineModel* timelineModel,
                    RoomListModel* roomModel, QListView* roomList,
                    ThreadHandler* threadHandler, QLabel* statusLabel,
                    QPointer<MainWindow> mw, QObject* parent = nullptr);

public slots:
    void showTimelineContextMenu(const QString& eventId, const QPoint& globalPos,
                                  const std::string& roomId);
    void onRoomListContextMenu(const QPoint& pos, const std::string& roomId);

signals:
    void roomLeft(const std::string& roomId);

private:
    std::shared_ptr<MatrixClient> client_;
    TimelineModel* timelineModel_;
    RoomListModel* roomModel_;
    QListView* roomList_;
    ThreadHandler* threadHandler_;
    QLabel* statusLabel_;
    QPointer<MainWindow> mw_;
};

} // namespace progressive::desktop
