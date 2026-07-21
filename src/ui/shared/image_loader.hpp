// src/ui/image_loader.hpp — async image + GIF loader with LRU cache.
#pragma once
#include <QImage>
#include <QMovie>
#include <QObject>
#include <QHash>
#include <QCache>
#include <functional>
#include <string>

namespace progressive::desktop {

class MatrixClient;

class ImageLoader : public QObject {
    Q_OBJECT
public:
    explicit ImageLoader(MatrixClient* client, QObject* parent = nullptr);

    // Update the client pointer (used after login).
    void setClient(MatrixClient* client) { client_ = client; }

    // Fetch a thumbnail (or full image if w/h=0). Calls callback on the
    // UI thread when done. Returns cached image immediately if available.
    void fetchThumbnail(const std::string& mxcUrl, int w, int h,
                         std::function<void(const QImage&)> cb);

    // Fetch an animated GIF as QMovie. Caller owns the movie (starts it).
    void fetchMovie(const std::string& mxcUrl,
                     std::function<void(QMovie*)> cb);

    // Check if image is in cache.
    bool hasImage(const std::string& mxcUrl) const;

    // Get from cache (returns empty if not cached).
    QImage getCached(const std::string& mxcUrl) const;

    // Change cache size. Default is 20. Set to 0 for unlimited (not recommended).
    void setCacheSize(int maxItems) { imageCache_.setMaxCost(maxItems > 0 ? maxItems : 1); }

    int cacheSize() const { return imageCache_.maxCost(); }

private:
    MatrixClient* client_;
    QCache<QString, QImage> imageCache_{20};
    QHash<QString, QMovie*> moviePool_;
};

} // namespace progressive::desktop
