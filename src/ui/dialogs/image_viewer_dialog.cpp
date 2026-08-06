// src/ui/image_viewer_dialog.cpp
#include "image_viewer_dialog.hpp"

#include <QApplication>
#include <QFileDialog>
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
#include <QMouseEvent>
#include <QWheelEvent>
#include <QShowEvent>

namespace progressive::desktop {

namespace {
inline constexpr int kViewerW = 800;
inline constexpr int kViewerH = 600;

// Guess a sensible extension for the temp/open-external file.
QString fallbackSuffix(const QString& suggestedName) {
    QString lower = suggestedName.toLower();
    if (lower.endsWith(".png") || lower.endsWith(".jpg") || lower.endsWith(".jpeg") ||
        lower.endsWith(".gif") || lower.endsWith(".webp") || lower.endsWith(".bmp"))
        return lower.mid(lower.lastIndexOf('.'));
    return ".png";
}
}  // namespace

ImageViewerDialog::ImageViewerDialog(const QImage& image, const QByteArray& rawBytes,
                                     const QString& suggestedName, const QString& mxcUrl,
                                     QWidget* parent)
    : QDialog(parent), image_(image), rawBytes_(rawBytes),
      suggestedName_(suggestedName), mxcUrl_(mxcUrl) {
    setWindowTitle("Image Viewer");
    setModal(true);
    resize(kViewerW, kViewerH);

    scene_ = new QGraphicsScene(this);
    if (!image_.isNull()) {
        item_ = scene_->addPixmap(QPixmap::fromImage(image_));
        item_->setTransformationMode(Qt::SmoothTransformation);
    }

    view_ = new QGraphicsView(scene_, this);
    view_->setBackgroundBrush(QColor(20, 20, 20));
    view_->setDragMode(QGraphicsView::ScrollHandDrag);
    view_->setRenderHint(QPainter::SmoothPixmapTransform);
    view_->setFrameShape(QFrame::NoFrame);

    auto* btnRow = new QHBoxLayout;
    zoomInBtn_ = new QPushButton("+", this);
    zoomOutBtn_ = new QPushButton("-", this);
    fitBtn_ = new QPushButton("Fit", this);
    saveBtn_ = new QPushButton("Save as…", this);
    openBtn_ = new QPushButton("Open externally", this);
    closeBtn_ = new QPushButton("Close", this);
    btnRow->addWidget(zoomInBtn_);
    btnRow->addWidget(zoomOutBtn_);
    btnRow->addWidget(fitBtn_);
    btnRow->addStretch();
    btnRow->addWidget(saveBtn_);
    btnRow->addWidget(openBtn_);
    btnRow->addWidget(closeBtn_);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->addWidget(view_);
    root->addLayout(btnRow);

    connect(zoomInBtn_, &QPushButton::clicked, this, &ImageViewerDialog::zoomIn);
    connect(zoomOutBtn_, &QPushButton::clicked, this, &ImageViewerDialog::zoomOut);
    connect(fitBtn_, &QPushButton::clicked, this, &ImageViewerDialog::fitToWindow);
    connect(saveBtn_, &QPushButton::clicked, this, &ImageViewerDialog::saveAs);
    connect(openBtn_, &QPushButton::clicked, this, &ImageViewerDialog::openExternally);
    connect(closeBtn_, &QPushButton::clicked, this, &QDialog::accept);

    setAttribute(Qt::WA_DeleteOnClose);
    fitToWindow();
}

void ImageViewerDialog::showEvent(QShowEvent* e) {
    QDialog::showEvent(e);
    // Element-style: open maximized (fullscreen feels natural on the PineTab).
    showMaximized();
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
    zoomLevel_ = view_->transform().m11();
}

void ImageViewerDialog::wheelEvent(QWheelEvent* e) {
    // Zoom centered on the cursor (Element parity).
    double factor = e->angleDelta().y() > 0 ? 1.25 : 1.0 / 1.25;
    const double target = qBound(0.05, zoomLevel_ * factor, 64.0);
    if (qFuzzyCompare(target, zoomLevel_)) return;
    QPointF anchor = view_->mapToScene(e->position().toPoint());
    zoomLevel_ = target;
    view_->resetTransform();
    view_->scale(zoomLevel_, zoomLevel_);
    view_->centerOn(anchor);
    e->accept();
}

void ImageViewerDialog::mouseDoubleClickEvent(QMouseEvent* e) {
    // Double-click toggles between fit and 100%.
    if (image_.isNull()) return;
    if (qFuzzyCompare(zoomLevel_, 1.0)) {
        fitToWindow();
    } else {
        zoomLevel_ = 1.0;
        view_->resetTransform();
        view_->centerOn(view_->mapToScene(e->position().toPoint()));
    }
    e->accept();
}

void ImageViewerDialog::saveAs() {
    if (image_.isNull()) return;
    QString base = suggestedName_;
    if (base.isEmpty()) base = "image.png";
    QString path = QFileDialog::getSaveFileName(this, "Save image", base,
        "All files (*.*)");
    if (path.isEmpty()) return;
    bool ok;
    if (!rawBytes_.isEmpty()) {
        QFile f(path);
        ok = f.open(QIODevice::WriteOnly) &&
             f.write(rawBytes_) == static_cast<qint64>(rawBytes_.size());
    } else {
        ok = image_.save(path);
    }
    if (!ok) QMessageBox::warning(this, "Error", "Failed to save the image.");
    else setWindowTitle("Image Viewer — saved");
}

void ImageViewerDialog::openExternally() {
    if (image_.isNull()) return;
    QString tmpDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (tmpDir.isEmpty()) tmpDir = "/tmp";
    QString path = tmpDir + "/progressive-desktop-image" + fallbackSuffix(suggestedName_);
    bool ok;
    if (!rawBytes_.isEmpty()) {
        QFile f(path);
        ok = f.open(QIODevice::WriteOnly) &&
             f.write(rawBytes_) == static_cast<qint64>(rawBytes_.size());
    } else {
        ok = image_.save(path);
    }
    if (!ok) {
        QMessageBox::warning(this, "Error", "Failed to save image to temp file.");
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

}  // namespace progressive::desktop
