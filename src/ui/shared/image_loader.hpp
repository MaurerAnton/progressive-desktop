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

// Cooldown for known-failed (404 etc.) mxcs before we try again. Avatars use
// a shorter window — a user can set an avatar mid-session and the room list
// must show it reasonably soon. IMPORTANT: the negative cache must NEVER
// engage on decryption failures (the key may arrive a second later) — only
// on HTTP failures (httpStatus >= 400).
inline constexpr qint64 kFailedMxcCooldownMs = 60 * 60 * 1000;
inline constexpr qint64 kFailedAvatarCooldownMs = 10 * 60 * 1000;

class ImageLoader : public QObject {
    Q_OBJECT
public:
    explicit ImageLoader(std::shared_ptr<MatrixClient> client, QObject* parent = nullptr);

    void setClient(std::shared_ptr<MatrixClient> client) { client_ = std::move(client); }

    // Fetch a thumbnail (or full image if w/h=0). Calls callback on the
    // UI thread when done. Returns cached image immediately if available.
    // context is a human-readable label for failure logging (e.g. "avatar").
    void fetchThumbnail(const std::string& mxcUrl, int w, int h,
                         std::function<void(const QImage&)> cb,
                         const std::string& context = std::string());

    // Fetch + decrypt m.encrypted media (file: object). Downloads the full
    // ciphertext, AES-256-CTR decrypts, verifies sha256, then loads the
    // image. Cache key is the mxc URL (same URL = same keys).
    void fetchEncryptedThumbnail(const std::string& mxcUrl,
        const std::string& key, const std::string& iv, const std::string& sha,
        std::function<void(const QImage&)> cb,
        const std::string& context = std::string());

    // Fetch an animated GIF as QMovie. Caller owns the movie (starts it).
    void fetchMovie(const std::string& mxcUrl,
                     std::function<void(QMovie*)> cb);

    // Check if image is in cache.
    bool hasImage(const std::string& mxcUrl) const;

    // Get from cache (returns empty if not cached).
    QImage getCached(const std::string& mxcUrl) const;

    // Change cache size. Default is 128. Set to 0 for unlimited (not recommended).
    void setCacheSize(int maxItems) { imageCache_.setMaxCost(maxItems > 0 ? maxItems : 1); }

    int cacheSize() const { return imageCache_.maxCost(); }

private:
    // Run one fetch on the thread pool, coalescing concurrent requests for
    // the same mxc. Returns true if this call started the fetch (and will
    // dispatch to all queued callbacks), false if it was queued behind one.
    bool beginFetch(const QString& key,
                    std::function<void(const QImage&)> cb,
                    std::function<void()> run);

    // True if this mxc is on the failure cooldown (skip the HTTP fetch).
    bool isFailed(const QString& key) const;
    void markFailed(const QString& key, const std::string& context);

    std::shared_ptr<MatrixClient> client_;
    QCache<QString, QImage> imageCache_{128};
    QHash<QString, QMovie*> moviePool_;
    // Negative cache: mxc -> time until which we skip re-fetching.
    QHash<QString, qint64> failedUntil_;
    // In-flight dedup: mxc -> queued callbacks waiting for the fetch.
    QHash<QString, QList<std::function<void(const QImage&)>>> inFlight_;
};

} // namespace progressive::desktop
