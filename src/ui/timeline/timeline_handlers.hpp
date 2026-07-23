// src/ui/timeline_handlers.hpp — timeline context menu action handlers.
#pragma once
#include <QPointer>
#include <string>
#include <memory>

class QWidget;
class QLabel;

namespace progressive::desktop {

class MatrixClient;
class TimelineModel;

void handleReaction(QPointer<QWidget> parent, const std::shared_ptr<MatrixClient>& client,
                     const std::string& roomId, const std::string& eventId,
                     TimelineModel* model, QLabel* statusLabel);

void handleEdit(QPointer<QWidget> parent, const std::shared_ptr<MatrixClient>& client,
                 const std::string& roomId, const std::string& eventId,
                 TimelineModel* model, QLabel* statusLabel);

void handleDelete(QPointer<QWidget> parent, const std::shared_ptr<MatrixClient>& client,
                   const std::string& roomId, const std::string& eventId,
                   TimelineModel* model, QLabel* statusLabel);

void handlePin(QPointer<QWidget> parent, const std::shared_ptr<MatrixClient>& client,
                const std::string& roomId, const std::string& eventId,
                TimelineModel* model, QLabel* statusLabel);

void handleCopyLink(QPointer<QWidget> parent, const std::string& roomId,
                     const std::string& eventId, QLabel* statusLabel);

} // namespace progressive::desktop
