// src/ui/image_viewer_dialog.cpp
#include "image_viewer_dialog.hpp"

#include <QGraphicsPixmapItem>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QDesktopServices>
#include <QUrl>
#include <QMessageBox>

namespace progressive::desktop {

ImageViewerDialog::ImageViewerDialog(const QImage& image, const QString& mxcUrl,
                                       QWidget* parent)
    : QDialog(parent), image_(image), mxcUrl_(mxcUrl) {
    setWindowTitle("Image Viewer");
    setModal(true);
    resize(800, 600);

    scene_ = new QGraphicsScene(this);
    if (!image_.isNull()) {
        auto* item = scene_->addPixmap(QPixmap::fromImage(image_));
        item->setTransformationMode(Qt::SmoothTransformation);
    }

    view_ = new QGraphicsView(scene_, this);
    view_->setBackgroundBrush(QColor(20, 20, 20));
    view_->setDragMode(QGraphicsView::ScrollHandDrag);

    auto* btnRow = new QHBoxLayout;
    zoomInBtn_ = new QPushButton("+", this);
    zoomOutBtn_ = new QPushButton("-", this);
    fitBtn_ = new QPushButton("Fit", this);
    openBtn_ = new QPushButton("Open externally", this);
    closeBtn_ = new QPushButton("Close", this);
    btnRow->addWidget(zoomInBtn_);
    btnRow->addWidget(zoomOutBtn_);
    btnRow->addWidget(fitBtn_);
    btnRow->addStretch();
    btnRow->addWidget(openBtn_);
    btnRow->addWidget(closeBtn_);

    auto* root = new QVBoxLayout(this);
    root->addWidget(view_);
    root->addLayout(btnRow);

    connect(zoomInBtn_, &QPushButton::clicked, this, &ImageViewerDialog::zoomIn);
    connect(zoomOutBtn_, &QPushButton::clicked, this, &ImageViewerDialog::zoomOut);
    connect(fitBtn_, &QPushButton::clicked, this, &ImageViewerDialog::fitToWindow);
    connect(openBtn_, &QPushButton::clicked, this, &ImageViewerDialog::openExternally);
    connect(closeBtn_, &QPushButton::clicked, this, &QDialog::accept);

    fitToWindow();
}

void ImageViewerDialog::zoomIn() {
    zoomLevel_ *= 1.25;
    view_->resetTransform();
    view_->scale(zoomLevel_, zoomLevel_);
}

void ImageViewerDialog::zoomOut() {
    zoomLevel_ /= 1.25;
    view_->resetTransform();
    view_->scale(zoomLevel_, zoomLevel_);
}

void ImageViewerDialog::fitToWindow() {
    if (image_.isNull()) return;
    auto sr = scene_->sceneRect();
    if (sr.width() <= 0 || sr.height() <= 0) return;
    view_->fitInView(sr, Qt::KeepAspectRatio);
    // Read back the applied transform to get the zoom level
    zoomLevel_ = view_->transform().m11();
}

void ImageViewerDialog::openExternally() {
    if (image_.isNull()) return;
    QString tmpDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QString path = tmpDir + "/progressive-desktop-image.png";
    if (!image_.save(path, "PNG")) {
        QMessageBox::warning(this, "Error", "Failed to save image to temp file.");
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

} // namespace progressive::desktop
