// src/ui/image_loader.cpp
#include "image_loader.hpp"
#include "core/matrix_client.hpp"

#include <QBuffer>
#include <QMetaObject>
#include <QThread>

namespace progressive::desktop {

ImageLoader::ImageLoader(MatrixClient* client, QObject* parent)
    : QObject(parent), client_(client) {}

void ImageLoader::fetchThumbnail(const std::string& mxcUrl, int w, int h,
                                   std::function<void(const QImage&)> cb) {
    QString key = QString::fromStdString(mxcUrl);
    if (auto* cached = imageCache_.object(key)) {
        cb(*cached);
        return;
    }

    std::thread([this, mxcUrl, key, w, h, cb]() {
        auto result = client_->downloadMedia(mxcUrl, w, h);
        QImage img;
        if (result.ok && !result.data.empty()) {
            img.loadFromData(result.data.data(), static_cast<int>(result.data.size()));
        }
        QMetaObject::invokeMethod(this, [this, key, img, cb]() {
            if (!img.isNull()) {
                imageCache_.insert(key, new QImage(img));
            }
            cb(img);
        }, Qt::QueuedConnection);
    }).detach();
}

void ImageLoader::fetchMovie(const std::string& mxcUrl,
                               std::function<void(QMovie*)> cb) {
    QString key = QString::fromStdString(mxcUrl);
    if (auto* existing = moviePool_.value(key)) {
        cb(existing);
        return;
    }

    std::thread([this, mxcUrl, key, cb]() {
        auto result = client_->downloadMedia(mxcUrl, 0, 0);
        QByteArray bytes;
        if (result.ok) {
            bytes = QByteArray(reinterpret_cast<const char*>(result.data.data()),
                               static_cast<int>(result.data.size()));
        }
        QMetaObject::invokeMethod(this, [this, key, bytes, cb]() {
            if (bytes.isEmpty()) { cb(nullptr); return; }
            auto* movie = new QMovie(this);
            movie->setFormat("GIF");
            auto* buf = new QBuffer(movie);
            buf->setData(bytes);
            buf->open(QIODevice::ReadOnly);
            movie->setDevice(buf);
            movie->setCacheMode(QMovie::CacheAll);
            moviePool_[key] = movie;
            cb(movie);
        }, Qt::QueuedConnection);
    }).detach();
}

bool ImageLoader::hasImage(const std::string& mxcUrl) const {
    return imageCache_.contains(QString::fromStdString(mxcUrl));
}

QImage ImageLoader::getCached(const std::string& mxcUrl) const {
    if (auto* cached = imageCache_.object(QString::fromStdString(mxcUrl))) {
        return *cached;
    }
    return {};
}

} // namespace progressive::desktop
