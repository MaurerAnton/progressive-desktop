#include "attachment_handler.hpp"
#include "core/matrix_client.hpp"
#include "core/thread_pool.hpp"
#include "../timeline/timeline_model.hpp"
#include "../dialogs/image_viewer_dialog.hpp"
#include <QCache>
#include <QDesktopServices>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QUrl>
#include <QWidget>
#include <chrono>
#include <cstdlib>

namespace progressive::desktop {

namespace {

// Full-image cache: opening the same image repeatedly must not re-download
// the file. Raw bytes keyed by mxc (unique per upload), bounded.
QCache<QString, QByteArray>& fullImageCache() {
    static QCache<QString, QByteArray> cache(48);
    return cache;
}

// Map a content-type to a file extension (".bin" when unknown).
QString extensionForMimetype(const QString& mt) {
    const QString m = mt.toLower();
    if (m == "image/png") return ".png";
    if (m == "image/jpeg" || m == "image/jpg") return ".jpg";
    if (m == "image/gif") return ".gif";
    if (m == "image/webp") return ".webp";
    if (m == "image/bmp") return ".bmp";
    if (m == "video/mp4") return ".mp4";
    if (m == "video/webm") return ".webm";
    if (m == "video/quicktime") return ".mov";
    if (m == "audio/mpeg") return ".mp3";
    if (m == "audio/ogg") return ".ogg";
    if (m == "audio/wav" || m == "audio/x-wav") return ".wav";
    if (m == "application/pdf") return ".pdf";
    if (m == "text/plain") return ".txt";
    if (m == "application/zip") return ".zip";
    return ".bin";
}

// Images without a mimetype default to .png, not .bin.
QString defaultSuffixForMsgtype(const QString& msgtype) {
    if (msgtype == "m.image") return ".png";
    if (msgtype == "m.video") return ".mp4";
    if (msgtype == "m.audio") return ".mp3";
    return ".bin";
}

// Real filename: prefer the event's body (m.file uses body = filename),
// fall back to a generic name + the mimetype's extension.
QString displayNameFor(const QString& body, const QString& msgtype,
                       const QString& mimetype) {
    const QString ext = mimetype.isEmpty()
        ? defaultSuffixForMsgtype(msgtype)
        : extensionForMimetype(mimetype);
    if (!body.isEmpty()) {
        if (body.contains('.')) return body;
        return body + ext;
    }
    if (msgtype == "m.image") return "image" + ext;
    if (msgtype == "m.video") return "video" + ext;
    if (msgtype == "m.audio") return "audio" + ext;
    return "file" + ext;
}

QString tempPathFor(const QString& name) {
    const char* tmpDir = getenv("TMPDIR");
    if (!tmpDir || !tmpDir[0]) tmpDir = "/tmp";
    return QString::fromStdString(std::string(tmpDir) + "/progressive_") +
        QString::number(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()) + "_" + name;
}

}  // namespace

AttachmentHandler::AttachmentHandler(std::shared_ptr<MatrixClient> client, TimelineModel* timelineModel,
                                     QLabel* statusLabel, QObject* parent)
    : QObject(parent), client_(std::move(client)), timelineModel_(timelineModel),
      statusLabel_(statusLabel) {}

void AttachmentHandler::openAttachment(const QString& eventId, const QString& mxcUrl) {
    if (mxcUrl.isEmpty()) return;

    int row = timelineModel_->findRow(eventId.toStdString());
    QString msgtype;
    QString body;
    QString mimetype;
    std::string mediaKey, mediaIv, mediaSha;
    if (row >= 0) {
        auto* evt = timelineModel_->at(row);
        if (evt) {
            msgtype = QString::fromStdString(evt->msgtype);
            body = QString::fromStdString(evt->body);
            mimetype = QString::fromStdString(evt->mimetype);
            mediaKey = evt->mediaKey;
            mediaIv = evt->mediaIv;
            mediaSha = evt->mediaSha256;
        }
    }

    std::string mxc = mxcUrl.toStdString();
    auto client = client_;
    QPointer<AttachmentHandler> guard(this);
    QString fileName = displayNameFor(body, msgtype, mimetype);

    if (msgtype == "m.video" || msgtype == "m.audio" || msgtype == "m.file") {
        if (statusLabel_) statusLabel_->setText("Downloading " + fileName + "...");
        ThreadPool::instance().enqueue([guard, client, mxc, fileName,
                                        mediaKey, mediaIv, mediaSha]() {
            auto r = mediaKey.empty()
                ? client->downloadMedia(mxc, 0, 0)
                : client->downloadMediaEncrypted(mxc, mediaKey, mediaIv, mediaSha);
            QMetaObject::invokeMethod(guard, [guard, r, fileName]() {
                if (guard.isNull()) return;
                if (!r.ok || r.data.empty()) {
                    QMessageBox::warning(nullptr, "Error", "Failed to download " + fileName + ".");
                    return;
                }
                QString tempPath = tempPathFor(fileName);
                QFile f(tempPath);
                if (!f.open(QIODevice::WriteOnly)) {
                    QMessageBox::warning(nullptr, "Error", "Failed to create temp file.");
                    return;
                }
                f.write(reinterpret_cast<const char*>(r.data.data()), r.data.size());
                f.close();
                QDesktopServices::openUrl(QUrl::fromLocalFile(tempPath));
            }, Qt::QueuedConnection);
        });
        return;
    }

    // Image: serve from the full-image cache when possible; on failure
    // fall back to the timeline thumbnail so the viewer still opens.
    QString qkey = mxcUrl;
    if (auto* cached = fullImageCache().object(qkey)) {
        QImage img;
        img.loadFromData(reinterpret_cast<const uchar*>(cached->constData()),
                         static_cast<int>(cached->size()));
        auto* dlg = new ImageViewerDialog(img, *cached, fileName, mxcUrl, nullptr);
        dlg->exec();
        return;
    }
    if (statusLabel_) statusLabel_->setText("Loading full image...");
    ThreadPool::instance().enqueue([guard, client, mxc, qkey, fileName, mediaKey, mediaIv, mediaSha]() {
        auto r = mediaKey.empty()
            ? client->downloadMedia(mxc, 0, 0)
            : client->downloadMediaEncrypted(mxc, mediaKey, mediaIv, mediaSha);
        QImage img;
        if (r.ok && !r.data.empty()) {
            img.loadFromData(r.data.data(), static_cast<int>(r.data.size()));
        }
        QByteArray raw;
        if (r.ok && !r.data.empty())
            raw = QByteArray(reinterpret_cast<const char*>(r.data.data()),
                             static_cast<int>(r.data.size()));
        QMetaObject::invokeMethod(guard, [guard, img, raw, qkey, fileName, mxc]() {
            if (guard.isNull()) return;
            if (img.isNull()) {
                QMessageBox::warning(nullptr, "Error", "Failed to load image.");
                return;
            }
            if (!raw.isEmpty()) fullImageCache().insert(qkey, new QByteArray(raw));
            auto* dlg = new ImageViewerDialog(img, raw, fileName, QString::fromStdString(mxc), nullptr);
            dlg->exec();
        }, Qt::QueuedConnection);
    });
}

} // namespace progressive::desktop
