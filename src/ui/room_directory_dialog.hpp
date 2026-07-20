// src/ui/room_directory_dialog.hpp — browse + join public Matrix rooms.
#pragma once
#include <QDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include "core/matrix_client.hpp"

namespace progressive::desktop {

class RoomDirectoryDialog : public QDialog {
    Q_OBJECT
public:
    explicit RoomDirectoryDialog(MatrixClient* client, QWidget* parent = nullptr);

    // Returns the room_id that was joined (empty if none).
    QString joinedRoomId() const { return joinedRoomId_; }

private slots:
    void onSearchClicked();
    void onLoadMoreClicked();
    void onJoinClicked(QListWidgetItem* item);

private:
    MatrixClient* client_;
    QString joinedRoomId_;
    QString nextBatch_;

    QLineEdit* searchEdit_;
    QLineEdit* serverEdit_;
    QPushButton* searchBtn_;
    QListWidget* resultsList_;
    QPushButton* loadMoreBtn_;
    QLabel* statusLabel_;
    QPushButton* closeBtn_;

    void doSearch(const std::string& query, const std::string& server, const std::string& from);
};

} // namespace progressive::desktop
