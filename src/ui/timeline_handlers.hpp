// src/ui/timeline_handlers.hpp — timeline context menu handlers.
#pragma once
#include <QString>
#include <string>
#include <QPointer>

namespace progressive::desktop {

class MainWindow;
class MatrixClient;

// Handle right-click context menu on a timeline message.
// Extracted from MainWindow::showTimelineContextMenu.
void handleTimelineContextMenu(MainWindow* win, const QString& eventId,
                                const QPoint& globalPos, MatrixClient* client,
                                const std::string& roomId);

} // namespace progressive::desktop
