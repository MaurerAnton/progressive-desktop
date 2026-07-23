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
    AccountSwitcher(MatrixClient* client, SessionStore* store, SyncEngine* sync,
                    QComboBox* accountCombo, QLabel* userLabel, QLabel* statusLabel,
                    RoomListModel* roomModel, TimelineModel* timelineModel,
                    ImageLoader* imageLoader, TimelineDelegate* timelineDelegate,
                    RoomHandler* roomHandler, ChatView* chatView,
                    QWidget* placeholder, QWidget* timelineView, QWidget* messageEdit,
                    QObject* parent = nullptr);

    void setClient(MatrixClient* c) { client_ = c; }

public slots:
    void switchAccount(int index);

private:
    MatrixClient* client_;
    SessionStore* store_;
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
