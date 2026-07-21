// src/ui/user_profile_dialog.hpp — view another user's profile + actions.
#pragma once
#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include "core/matrix_client.hpp"

namespace progressive::desktop {

class UserProfileDialog : public QDialog {
    Q_OBJECT
public:
    explicit UserProfileDialog(MatrixClient* client, const std::string& roomId,
                                const std::string& userId, QWidget* parent = nullptr);

private slots:
    void onSendDM();
    void onKick();
    void onBan();
    void onPromote();
    void onDemote();
    void onCopyMXID();

private:
    MatrixClient* client_;
    std::string roomId_;
    std::string userId_;
    QLabel* avatarLabel_;
    QLabel* nameLabel_;
    QLabel* idLabel_;
    QLabel* powerLabel_;
    QLabel* statusLabel_;
    QPushButton* dmBtn_;
    QPushButton* kickBtn_;
    QPushButton* banBtn_;
    QPushButton* promoteBtn_;
    QPushButton* demoteBtn_;
    QPushButton* copyBtn_;
    QPushButton* closeBtn_;
    int currentPowerLevel_ = 0;

    void loadProfile();
};

} // namespace progressive::desktop
