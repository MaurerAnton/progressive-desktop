// src/ui/room_members_dialog.hpp — room member list with search.
#pragma once
#include <QDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <map>
#include <vector>
#include <string>
#include "core/matrix_client.hpp"

namespace progressive::desktop {

struct MemberInfo {
    std::string userId;
    std::string displayName;
    std::string avatarUrl;
    std::string membership;
};

namespace progressive::desktop { class SessionStore; }

class RoomMembersDialog : public QDialog {
    Q_OBJECT
public:
    explicit RoomMembersDialog(MatrixClient* client, const std::string& roomId,
                                QWidget* parent = nullptr);
    void setSessionStore(SessionStore* s) { store_ = s; }

private slots:
    void onSearchChanged();
    void onMemberClicked(QListWidgetItem* item);
    void onMemberContextMenu(const QPoint& pos);

signals:
    void verifyRequested(const QString& userId, const QString& deviceId);

private:
    MatrixClient* client_;
    SessionStore* store_ = nullptr;
    std::string roomId_;
    QLineEdit* searchEdit_;
    QListWidget* list_;
    QLabel* statusLabel_;
    QPushButton* closeBtn_;
    QTimer* debounceTimer_;
    std::vector<MemberInfo> allMembers_;
    std::map<std::string, int> userTrust_;  // userId -> 0 unverified, 1 trusted, 2 verified
    bool loaded_ = false;

    void loadMembers();
    void applyFilter();
    void startVerifyForUser(const QString& userId);
};

} // namespace progressive::desktop
