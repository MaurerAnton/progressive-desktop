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
    AttachmentHandler(MatrixClient* client, TimelineModel* timelineModel,
                      QLabel* statusLabel, QObject* parent = nullptr);

    void openAttachment(const QString& eventId, const QString& mxcUrl);

private:
    MatrixClient* client_;
    TimelineModel* timelineModel_;
    QLabel* statusLabel_ = nullptr;
};

} // namespace progressive::desktop
