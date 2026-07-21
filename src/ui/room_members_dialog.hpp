// src/ui/room_members_dialog.hpp — room member list with search.
#pragma once
#include <QDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
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

class RoomMembersDialog : public QDialog {
    Q_OBJECT
public:
    explicit RoomMembersDialog(MatrixClient* client, const std::string& roomId,
                                QWidget* parent = nullptr);

private slots:
    void onSearchChanged();
    void onMemberClicked(QListWidgetItem* item);

private:
    MatrixClient* client_;
    std::string roomId_;
    QLineEdit* searchEdit_;
    QListWidget* list_;
    QLabel* statusLabel_;
    QPushButton* closeBtn_;
    QTimer* debounceTimer_;
    std::vector<MemberInfo> allMembers_;
    bool loaded_ = false;

    void loadMembers();
    void applyFilter();
};

} // namespace progressive::desktop
