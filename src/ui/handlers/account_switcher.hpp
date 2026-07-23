#pragma once
#include <QObject>
#include <string>

class QComboBox;
class QLabel;
class QWidget;

namespace progressive::desktop {

class MatrixClient;
class SessionStore;
class SyncEngine;
class RoomListModel;
class TimelineModel;
class ImageLoader;
class TimelineDelegate;
class RoomHandler;
class ChatView;

class AccountSwitcher : public QObject {
    Q_OBJECT
public:
    AccountSwitcher(std::shared_ptr<MatrixClient> client, std::shared_ptr<SessionStore> store, SyncEngine* sync,
                    QComboBox* accountCombo, QLabel* userLabel, QLabel* statusLabel,
                    RoomListModel* roomModel, TimelineModel* timelineModel,
                    ImageLoader* imageLoader, TimelineDelegate* timelineDelegate,
                    RoomHandler* roomHandler, ChatView* chatView,
                    QWidget* placeholder, QWidget* timelineView, QWidget* messageEdit,
                    QObject* parent = nullptr);

    void setClient(std::shared_ptr<MatrixClient> c) { client_ = std::move(c); }

public slots:
    void switchAccount(int index);

private:
    std::shared_ptr<MatrixClient> client_;
    std::shared_ptr<SessionStore> store_;
    SyncEngine* sync_;
    QComboBox* accountCombo_;
    QLabel* userLabel_;
    QLabel* statusLabel_;
    RoomListModel* roomModel_;
    TimelineModel* timelineModel_;
    ImageLoader* imageLoader_;
    TimelineDelegate* timelineDelegate_;
    RoomHandler* roomHandler_;
    ChatView* chatView_;
    QWidget* placeholder_;
    QWidget* timelineView_;
    QWidget* messageEdit_;
};

} // namespace progressive::desktop
