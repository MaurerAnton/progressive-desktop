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

class TimelineModel;

// Lists every thread in a room (root preview + reply count) and hands the
// chosen root back to the room for the thread view (Element-style). Root
// previews come from the local (decrypted) timeline when available.
class ThreadsDialog : public QDialog {
    Q_OBJECT
public:
    explicit ThreadsDialog(MatrixClient* client, const std::string& roomId,
                           TimelineModel* timelineModel = nullptr,
                           std::function<void(const QString& rootEventId)> onOpenThread = nullptr,
                           QWidget* parent = nullptr);

private slots:
    void onRefreshClicked();
    void onLoadMoreClicked();
    void onThreadClicked(QListWidgetItem* item);

private:
    MatrixClient* client_;
    std::string roomId_;
    TimelineModel* timelineModel_;
    std::function<void(const QString&)> onOpenThread_;
    QListWidget* list_;
    QLabel* statusLabel_;
    QPushButton* refreshBtn_;
    QPushButton* moreBtn_;
    QPushButton* closeBtn_;
    std::string nextBatch_;  // /threads pagination cursor

    void loadThreads();
};

} // namespace progressive::desktop
