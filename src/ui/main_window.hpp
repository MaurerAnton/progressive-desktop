// src/ui/main_window.hpp — Phase 3 main window with full features.
#pragma once
#include "core/matrix_client.hpp"
#include "core/session_store.hpp"
#include "core/sync_engine.hpp"
#include "core/fast_sync.hpp"
#include "core/notifications.hpp"
#include "room_list_model.hpp"
#include "room_list_delegate.hpp"
#include "timeline_model.hpp"
#include "timeline_delegate.hpp"
#include "image_loader.hpp"
#include "message_edit.hpp"

#include <QMainWindow>
#include <QListView>
#include <QSplitter>
#include <QLabel>
#include <QPointer>
#include <unordered_map>

class QToolBar;
class QAction;
class QLabel;

namespace progressive::desktop {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

    void setClient(MatrixClient* client) { client_ = client; }
    void setSessionStore(SessionStore* store) { store_ = store; }

    void startWithSavedSession();
    void forceReLogin();

    void onSync(const FastSyncResponse& resp);
    void onSyncState(SyncEngineState state, const SyncEngineStats& stats);

protected:
    void keyPressEvent(QKeyEvent* e) override;
    void closeEvent(QCloseEvent* e) override;

private slots:
    void onRoomClicked(const QModelIndex& idx);
    void onRoomListContextMenu(const QPoint& pos);
    void onSendMessage(const std::string& body);
    void onSlashCommand(const std::string& cmd, const std::string& args);
    void onLogoutClicked();
    void onLoginDialogAccepted();
    void onNewChatClicked();
    void onJoinRoomClicked();
    void onBrowseRoomsClicked();
    void onSettingsClicked();
    void onToggleFullscreen();
    void onAllThreadsClicked();
    void onRoomSettingsClicked();
    void onImageClicked(const QString& eventId, const QString& mxcUrl);
    void onMessageClicked(const QString& eventId);
    void onTimelineContextMenu(const QPoint& pos);
    void onAttachFile(const QString& filePath);
    void openThreadView(const QString& rootEventId);
    void closeThreadView();

private:
    void rebuildRoomList(const FastSyncResponse& resp);
    void batchLoadRoomStates();  // lazy-fetch missing room names/avatars after first sync
    void appendTimelineForRoom(const std::string& roomId, const std::vector<FastEvent>& events,
                                const std::unordered_map<std::string, std::string>* memberAvatars = nullptr);
    void loadRoomHistory(const std::string& roomId);
    void wireSyncCallbacks();
    void showLoginDialog();
    void updateRoomListHeader();
    DisplayedEvent fastEventToDisplayed(const FastEvent& fe);
    void showTimelineContextMenu(const QString& eventId, const QPoint& globalPos);
    void onRoomJoined(const std::string& roomId);

    MatrixClient* client_ = nullptr;
    SessionStore* store_ = nullptr;
    SyncEngine sync_;
    ImageLoader* imageLoader_ = nullptr;
    DesktopNotifier notifier_;

    QToolBar* toolbar_ = nullptr;
    QLabel* userLabel_ = nullptr;
    QAction* newChatAction_ = nullptr;
    QAction* joinRoomAction_ = nullptr;
    QAction* browseRoomsAction_ = nullptr;
    QAction* allThreadsAction_ = nullptr;
    QAction* roomSettingsAction_ = nullptr;
    QAction* settingsAction_ = nullptr;
    QAction* fullscreenAction_ = nullptr;
    QAction* logoutAction_ = nullptr;
    QLabel* roomListHeader_ = nullptr;
    QLabel* inviteHeader_ = nullptr;  // "⬇ Invitations (N)" — visible when invites exist
    QLabel* timelinePlaceholder_ = nullptr;

    QSplitter* splitter_ = nullptr;
    QListView* roomList_ = nullptr;
    RoomListModel* roomModel_ = nullptr;
    RoomListDelegate* roomListDelegate_ = nullptr;

    // Timeline (replaces old TimelineView)
    QListView* timelineView_ = nullptr;
    TimelineModel* timelineModel_ = nullptr;
    TimelineDelegate* timelineDelegate_ = nullptr;

    MessageEdit* messageEdit_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* threadBanner_ = nullptr;  // "← Back to chat" banner (visible in thread mode)

    QString currentRoomId_;
    QString currentThreadRoot_;  // if non-empty, we're viewing a thread (not main chat)
    bool isFullscreen_ = false;
};

} // namespace progressive::desktop
