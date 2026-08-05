// src/ui/image_loader.cpp
#include "image_loader.hpp"
#include "core/matrix_client.hpp"

#include <QBuffer>
#include <QMetaObject>
#include <QThread>
#include "core/thread_pool.hpp"

namespace progressive::desktop {

// Bounds the GIF movie pool — otherwise it grows without limit on small devices.
static const int kMaxCachedMovies = 20;

ImageLoader::ImageLoader(std::shared_ptr<MatrixClient> client, QObject* parent)
    : QObject(parent), client_(std::move(client)) {}

void ImageLoader::fetchThumbnail(const std::string& mxcUrl, int w, int h,
                                   std::function<void(const QImage&)> cb) {
    QString key = QString::fromStdString(mxcUrl);
    if (auto* cached = imageCache_.object(key)) {
        cb(*cached);
        return;
    }
    if (!client_) { cb(QImage()); return; }

    ThreadPool::instance().enqueue([this, mxcUrl, key, w, h, cb]() {
        auto result = client_->downloadMedia(mxcUrl, w, h);
        QImage img;
        if (result.ok && !result.data.empty()) {
            img.loadFromData(result.data.data(), static_cast<int>(result.data.size()));
        }
        if (img.isNull() && (w > 0 || h > 0)) {
            // Some servers/CDNs 404 the thumbnail endpoint — fall back to the
            // full file so avatars still render.
            auto full = client_->downloadMedia(mxcUrl, 0, 0);
            if (full.ok && !full.data.empty())
                img.loadFromData(full.data.data(), static_cast<int>(full.data.size()));
        }
        QMetaObject::invokeMethod(this, [this, key, img, cb]() {
            if (!img.isNull()) {
                imageCache_.insert(key, new QImage(img));
            }
            cb(img);
        }, Qt::QueuedConnection);
    });
}

void ImageLoader::fetchEncryptedThumbnail(const std::string& mxcUrl,
    const std::string& key, const std::string& iv, const std::string& sha,
    std::function<void(const QImage&)> cb) {
    QString qkey = QString::fromStdString(mxcUrl);
    if (auto* cached = imageCache_.object(qkey)) {
        cb(*cached);
        return;
    }
    if (!client_ || key.empty() || iv.empty()) { cb(QImage()); return; }

    ThreadPool::instance().enqueue([this, mxcUrl, qkey, key, iv, sha, cb]() {
        auto result = client_->downloadMediaEncrypted(mxcUrl, key, iv, sha);
        QImage img;
        if (result.ok && !result.data.empty()) {
            img.loadFromData(result.data.data(), static_cast<int>(result.data.size()));
        }
        QMetaObject::invokeMethod(this, [this, qkey, img, cb]() {
            if (!img.isNull()) {
                imageCache_.insert(qkey, new QImage(img));
            }
            cb(img);
        }, Qt::QueuedConnection);
    });
}

void ImageLoader::fetchMovie(const std::string& mxcUrl,
                               std::function<void(QMovie*)> cb) {
    QString key = QString::fromStdString(mxcUrl);
    if (auto* existing = moviePool_.value(key)) {
        cb(existing);
        return;
    }
    if (!client_) { cb(nullptr); return; }

    ThreadPool::instance().enqueue([this, mxcUrl, key, cb]() {
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
            if (moviePool_.size() >= kMaxCachedMovies && !moviePool_.contains(key)) {
                auto it = moviePool_.begin();
                if (it != moviePool_.end()) {
                    (*it)->deleteLater();
                    moviePool_.erase(it);
                }
            }
            moviePool_[key] = movie;
            cb(movie);
        }, Qt::QueuedConnection);
    });
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
