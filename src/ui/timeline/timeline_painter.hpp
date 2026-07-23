// src/ui/timeline/timeline_painter.hpp — timeline rendering functions.
#pragma once
#include <QPainter>
#include <QRect>
#include <QModelIndex>
#include <QString>
#include <unordered_set>

namespace progressive::desktop {

class ImageLoader;
class TimelineModel;

namespace timeline_layout { struct BubbleLayout; }

namespace timeline_render {

void drawSystemRow(QPainter* p, const QRect& rect, const QModelIndex& idx,
                   const QString& type);
void drawMessageBubble(QPainter* p, const QRect& rowRect, const QModelIndex& idx,
                       const timeline_layout::BubbleLayout& L,
                       const QString& myUserId, ImageLoader* loader,
                       std::unordered_set<std::string>& pendingFetches);

} // namespace timeline_render
} // namespace progressive::desktop
