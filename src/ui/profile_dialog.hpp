// src/ui/profile_dialog.hpp — edit own display name + avatar.
#pragma once
#include <QDialog>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include "core/matrix_client.hpp"

namespace progressive::desktop {

class ProfileDialog : public QDialog {
    Q_OBJECT
public:
    ProfileDialog(MatrixClient* client, QWidget* parent = nullptr);

private slots:
    void onSaveNameClicked();
    void onSetAvatarClicked();

private:
    MatrixClient* client_;
    QLineEdit* nameEdit_;
    QLabel* avatarPreview_;
    QPushButton* saveNameBtn_;
    QPushButton* setAvatarBtn_;
    QPushButton* closeBtn_;
    QLabel* statusLabel_;
    std::string currentAvatarMxc_;
};

} // namespace progressive::desktop
