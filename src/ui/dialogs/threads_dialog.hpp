// src/ui/threads_dialog.hpp — view all threads in a room + open thread.
#pragma once
#include <QDialog>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include "core/matrix_client.hpp"

namespace progressive::desktop {

class ThreadsDialog : public QDialog {
    Q_OBJECT
public:
    explicit ThreadsDialog(MatrixClient* client, const std::string& roomId,
                             QWidget* parent = nullptr);

private slots:
    void onRefreshClicked();
    void onThreadDoubleClicked(QListWidgetItem* item);

private:
    MatrixClient* client_;
    std::string roomId_;
    QListWidget* list_;
    QLabel* statusLabel_;
    QPushButton* refreshBtn_;
    QPushButton* closeBtn_;

    void loadThreads();
};

} // namespace progressive::desktop
