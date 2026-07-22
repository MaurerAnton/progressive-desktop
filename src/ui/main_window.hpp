// src/ui/main_window.hpp — Phase 3 main window with full features.
#pragma once
#include "core/matrix_client.hpp"
#include "core/session_store.hpp"
#include "core/sync_engine.hpp"
#include "core/fast_sync.hpp"
#include "notifications.hpp"
#include "room_list_model.hpp"
#include "room_list_delegate.hpp"
#include "timeline/timeline_model.hpp"
#include "timeline/timeline_delegate.hpp"
#include "shared/image_loader.hpp"
#include "chat/message_edit.hpp"
#include "chat/chat_view.hpp"
#include "room/room_store.hpp"
#include "handlers/auth_handler.hpp"

#include <QMainWindow>
#include <QListView>
#include <QSplitter>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QPointer>

class QToolBar;
class QAction;
class QLabel;

namespace progressive::desktop {

class ToolbarHandler;
class RoomHandler;
class AttachmentHandler;
class SlashCommandHandler;
class SyncResponseHandler;
class AccountSwitcher;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

    void setClient(MatrixClient* client) { client_ = client; }
    void setSessionStore(SessionStore* store) { store_ = store; }
    void startWithSavedSession();

    void onSyncState(SyncEngineState state, const SyncEngineStats& stats);

    QLabel* threadBanner() const { return threadBanner_; }
    QPushButton* threadBtn() const { return threadBtn_; }
    ChatView* chatView() const { return chatView_; }
    RoomStore* roomStore() const { return roomStore_; }
    RoomListModel* roomModel() const { return roomModel_; }
    TimelineModel* timelineModel() const { return timelineModel_; }
    QLabel* statusLabel() const { return statusLabel_; }
    QLabel* inviteHead() const { return inviteHeader_; }
    MessageEdit* messageEdit() const { return messageEdit_; }
    QListView* timelineView() const { return timelineView_; }
    QLabel* placeholder() const { return timelinePlaceholder_; }
    QPushButton* loadMoreBtn() const { return loadMoreBtn_; }
    QPushButton* chatLogBtn() const { return chatLogBtn_; }
    RoomHandler* roomHandler() const { return roomHandler_; }

protected:
    void keyPressEvent(QKeyEvent* e) override;
    void closeEvent(QCloseEvent* e) override;

private slots:
    void onMessageClicked(const QString& eventId);

private:
    void wireSyncCallbacks();

    MatrixClient* client_ = nullptr;
    SessionStore* store_ = nullptr;
    SyncEngine sync_;
    ImageLoader* imageLoader_ = nullptr;
    DesktopNotifier notifier_;

    QToolBar* toolbar_ = nullptr;
    QLabel* userLabel_ = nullptr;
    QComboBox* accountCombo_ = nullptr;
    QAction* logoutAction_ = nullptr;
    QLabel* roomListHeader_ = nullptr;
    QLabel* inviteHeader_ = nullptr;
    QLabel* timelinePlaceholder_ = nullptr;

    QSplitter* splitter_ = nullptr;
    QListView* roomList_ = nullptr;
    RoomListModel* roomModel_ = nullptr;
    RoomListDelegate* roomListDelegate_ = nullptr;

    QListView* timelineView_ = nullptr;
    TimelineModel* timelineModel_ = nullptr;
    TimelineDelegate* timelineDelegate_ = nullptr;

    MessageEdit* messageEdit_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* threadBanner_ = nullptr;
    QPushButton* loadMoreBtn_ = nullptr;
    QPushButton* chatLogBtn_ = nullptr;
    QPushButton* threadBtn_ = nullptr;
    ChatView* chatView_ = nullptr;
    RoomStore* roomStore_ = nullptr;
    AuthHandler* auth_ = nullptr;
    ToolbarHandler* toolbarHandler_ = nullptr;
    RoomHandler* roomHandler_ = nullptr;
    SyncResponseHandler* syncHandler_ = nullptr;
    AttachmentHandler* attachmentHandler_ = nullptr;
    SlashCommandHandler* slashHandler_ = nullptr;
    AccountSwitcher* accountSwitcher_ = nullptr;
};

} // namespace progressive::desktop
