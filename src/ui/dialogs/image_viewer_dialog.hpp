// src/ui/image_viewer_dialog.hpp — fullscreen image viewer with zoom.
#pragma once
#include <QDialog>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
class QGraphicsView;
class QGraphicsScene;

namespace progressive::desktop {

class ImageViewerDialog : public QDialog {
    Q_OBJECT
public:
    explicit ImageViewerDialog(const QImage& image, const QString& mxcUrl,
                                 QWidget* parent = nullptr);

private slots:
    void zoomIn();
    void zoomOut();
    void fitToWindow();
    void openExternally();

private:
    QImage image_;
    QString mxcUrl_;
    QGraphicsView* view_;
    QGraphicsScene* scene_;
    QPushButton* zoomInBtn_;
    QPushButton* zoomOutBtn_;
    QPushButton* fitBtn_;
    QPushButton* openBtn_;
    QPushButton* closeBtn_;
    double zoomLevel_ = 1.0;
};

} // namespace progressive::desktop
