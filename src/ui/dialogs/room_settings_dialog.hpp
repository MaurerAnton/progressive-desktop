// src/ui/room_settings_dialog.hpp — room settings: topic, name, members, power levels.
#pragma once
#include <QDialog>
#include <QLineEdit>
#include <QTextEdit>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include "core/matrix_client.hpp"

namespace progressive::desktop {

class RoomSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit RoomSettingsDialog(MatrixClient* client, const std::string& roomId,
                                  const std::string& roomName, QWidget* parent = nullptr);

private slots:
    void onSaveTopicClicked();
    void onSaveNameClicked();
    void onRefreshMembersClicked();
    void onMemberContextMenu(const QPoint& pos);

private:
    MatrixClient* client_;
    std::string roomId_;

    QLineEdit* nameEdit_;
    QTextEdit* topicEdit_;
    QPushButton* saveNameBtn_;
    QPushButton* saveTopicBtn_;
    QListWidget* membersList_;
    QPushButton* refreshMembersBtn_;
    QLabel* statusLabel_;
    QPushButton* closeBtn_;

    void loadMembers();
    void showMemberMenu(const QString& userId, const QPoint& globalPos);
};

} // namespace progressive::desktop
