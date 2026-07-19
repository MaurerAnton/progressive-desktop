// src/ui/main_window.hpp — top-level window for Phase 2.
//
// Layout:
//   ┌──────────────┬───────────────────────────┐
//   │ Room list    │ Timeline                  │
//   │ (sidebar)    │ (markdown bubbles)        │
//   │              │                           │
//   │              ├───────────────────────────┤
//   │              │ MessageEdit (input)        │
//   └──────────────┴───────────────────────────┘
//
// SyncEngine runs in a worker thread, calls onSync callback on the UI thread
// (via Qt::QueuedConnection) to update the room list + timeline.

#pragma once

#include "core/matrix_client.hpp"
#include "core/session_store.hpp"
#include "core/sync_engine.hpp"
#include "room_list_model.hpp"
#include "timeline_view.hpp"
#include "message_edit.hpp"

#include <QMainWindow>
#include <QListView>
#include <QSplitter>
#include <QLabel>
#include <QPointer>

namespace progressive::desktop {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

    void setClient(MatrixClient* client) { client_ = client; }
    void setSessionStore(SessionStore* store) { store_ = store; }

    // Called once at startup if a saved session exists — starts sync.
    void startWithSavedSession();

    // Public UI-thread slots — called from SyncEngine callbacks (marshaled
    // via QMetaObject::invokeMethod in main.cpp).
    void onSync(const FastSyncResponse& resp);
    void onSyncState(SyncEngineState state, const SyncEngineStats& stats);

protected:
    void closeEvent(QCloseEvent* e) override;

private slots:
    void onRoomClicked(const QModelIndex& idx);
    void onSendMessage(const std::string& body);
    void onSlashCommand(const std::string& cmd, const std::string& args);

private:
    void rebuildRoomList(const FastSyncResponse& resp);
    void appendTimelineForRoom(const std::string& roomId,
                                const std::vector<FastEvent>& events);
    void wireSyncCallbacks();

    MatrixClient* client_ = nullptr;
    SessionStore* store_ = nullptr;
    SyncEngine sync_;        // owns the worker thread

    QSplitter* splitter_ = nullptr;
    QListView* roomList_ = nullptr;
    RoomListModel* roomModel_ = nullptr;
    TimelineView* timeline_ = nullptr;
    MessageEdit* messageEdit_ = nullptr;
    QLabel* statusLabel_ = nullptr;

    QString currentRoomId_;
};

} // namespace progressive::desktop
