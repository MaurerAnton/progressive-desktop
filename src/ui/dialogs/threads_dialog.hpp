// src/ui/threads_dialog.hpp — view all threads in a room + open thread.
#pragma once
#include <QDialog>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <functional>
#include <string>
#include "core/matrix_client.hpp"

namespace progressive::desktop {

// Lists every thread in a room (root preview + reply count) and hands the
// chosen root back to the room for the thread view (Element-style).
class ThreadsDialog : public QDialog {
    Q_OBJECT
public:
    explicit ThreadsDialog(MatrixClient* client, const std::string& roomId,
                           std::function<void(const QString& rootEventId)> onOpenThread,
                           QWidget* parent = nullptr);

private slots:
    void onRefreshClicked();
    void onThreadDoubleClicked(QListWidgetItem* item);

private:
    MatrixClient* client_;
    std::string roomId_;
    std::function<void(const QString&)> onOpenThread_;
    QListWidget* list_;
    QLabel* statusLabel_;
    QPushButton* refreshBtn_;
    QPushButton* closeBtn_;

    void loadThreads();
};

} // namespace progressive::desktop
