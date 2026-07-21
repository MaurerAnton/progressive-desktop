// src/ui/room_members_dialog.hpp — room member list with search.
#pragma once
#include <QDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <vector>
#include <string>
#include "core/matrix_client.hpp"

namespace progressive::desktop {

struct MemberInfo {
    std::string userId;
    std::string displayName;
    std::string avatarUrl;
    std::string membership;
    int powerLevel = 0;
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
    std::vector<MemberInfo> allMembers_;

    void loadMembers();
};

} // namespace progressive::desktop
