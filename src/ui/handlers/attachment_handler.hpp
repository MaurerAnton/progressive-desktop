#pragma once
#include <QObject>
#include <QPointer>
#include <QString>
#include <string>

class QLabel;

namespace progressive::desktop {

class MatrixClient;
class TimelineModel;

class AttachmentHandler : public QObject {
    Q_OBJECT
public:
    AttachmentHandler(std::shared_ptr<MatrixClient> client, TimelineModel* timelineModel,
                      QLabel* statusLabel, QObject* parent = nullptr);

    void setClient(std::shared_ptr<MatrixClient> c) { client_ = std::move(c); }

    void openAttachment(const QString& eventId, const QString& mxcUrl);

private:
    std::shared_ptr<MatrixClient> client_;
    TimelineModel* timelineModel_;
    QLabel* statusLabel_ = nullptr;
};

} // namespace progressive::desktop
