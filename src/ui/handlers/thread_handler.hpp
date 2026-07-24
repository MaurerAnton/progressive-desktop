// src/ui/handlers/thread_handler.hpp — thread view management.
#pragma once
#include <QObject>
#include <QPointer>
#include <QString>
#include <string>

class QLabel;

namespace progressive::desktop {

class MatrixClient;
class TimelineModel;
class MainWindow;

class ThreadHandler : public QObject {
    Q_OBJECT
public:
    ThreadHandler(std::shared_ptr<MatrixClient> client, TimelineModel* timelineModel,
                  QLabel* threadBanner, QLabel* statusLabel,
                  QPointer<MainWindow> mw, QObject* parent = nullptr);

    void setClient(std::shared_ptr<MatrixClient> c) { client_ = std::move(c); }

    const std::string& currentThreadRoot() const { return currentThreadRoot_; }
    void clearThreadRoot() { currentThreadRoot_.clear(); }

public slots:
    void openThreadView(const QString& rootEventId, const std::string& roomId);
    void closeThreadView(const std::string& roomId);
    void replyInThread(const QString& eventId, const std::string& roomId);

private:
    void sendThreadReply(const std::string& roomId, const std::string& threadRoot,
                         const std::string& replyToEventId, const std::string& text);

    std::shared_ptr<MatrixClient> client_;
    TimelineModel* timelineModel_;
    QLabel* threadBanner_;
    QLabel* statusLabel_;
    QPointer<MainWindow> mw_;
    std::string currentThreadRoot_;
};

} // namespace progressive::desktop
