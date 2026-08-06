// src/ui/image_viewer_dialog.hpp — fullscreen image viewer with zoom.
#pragma once
#include <QDialog>
#include <QImage>
#include <QPushButton>
#include <QByteArray>
class QGraphicsView;
class QGraphicsScene;
class QGraphicsPixmapItem;
class QWheelEvent;
class QMouseEvent;

namespace progressive::desktop {

// Element-style lightbox: maximized dark viewer, wheel/double-click zoom
// centered on the cursor, click-outside to close, Save-as with the ORIGINAL
// bytes (no re-encode).
class ImageViewerDialog : public QDialog {
    Q_OBJECT
public:
    ImageViewerDialog(const QImage& image, const QByteArray& rawBytes,
                      const QString& suggestedName, const QString& mxcUrl,
                      QWidget* parent = nullptr);

protected:
    void showEvent(QShowEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;

private slots:
    void zoomIn();
    void zoomOut();
    void fitToWindow();
    void saveAs();
    void openExternally();

private:
    QImage image_;
    QByteArray rawBytes_;
    QString suggestedName_;
    QString mxcUrl_;
    QGraphicsView* view_;
    QGraphicsScene* scene_;
    QGraphicsPixmapItem* item_ = nullptr;
    QPushButton* zoomInBtn_;
    QPushButton* zoomOutBtn_;
    QPushButton* fitBtn_;
    QPushButton* saveBtn_;
    QPushButton* openBtn_;
    QPushButton* closeBtn_;
    double zoomLevel_ = 1.0;
};

} // namespace progressive::desktop
