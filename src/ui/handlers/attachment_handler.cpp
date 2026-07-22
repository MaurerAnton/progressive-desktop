#include "attachment_handler.hpp"
#include "core/matrix_client.hpp"
#include "core/thread_pool.hpp"
#include "../timeline/timeline_model.hpp"
#include "../dialogs/image_viewer_dialog.hpp"
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

AttachmentHandler::AttachmentHandler(MatrixClient* client, TimelineModel* timelineModel,
                                     QLabel* statusLabel, QObject* parent)
    : QObject(parent), client_(client), timelineModel_(timelineModel),
      statusLabel_(statusLabel) {}

void AttachmentHandler::openAttachment(const QString& eventId, const QString& mxcUrl) {
    if (mxcUrl.isEmpty()) return;

    int row = timelineModel_->findRow(eventId.toStdString());
    QString msgtype;
    if (row >= 0) {
        auto* evt = timelineModel_->at(row);
        if (evt) {
            msgtype = QString::fromStdString(evt->msgtype);
        }
    }

    std::string mxc = mxcUrl.toStdString();
    MatrixClient* client = client_;
    QPointer<AttachmentHandler> guard(this);

    if (msgtype == "m.video" || msgtype == "m.audio" || msgtype == "m.file") {
        if (statusLabel_) statusLabel_->setText("Downloading " + msgtype.mid(2) + "...");
        ThreadPool::instance().enqueue([guard, client, mxc, msgtype]() {
            auto r = client->downloadMedia(mxc, 0, 0);
            QMetaObject::invokeMethod(guard, [guard, r, msgtype]() {
                if (guard.isNull()) return;
                if (!r.ok || r.data.empty()) {
                    QMessageBox::warning(nullptr, "Error", "Failed to download " + msgtype + ".");
                    return;
                }
                QString suffix = msgtype == "m.video" ? ".mp4" : (msgtype == "m.audio" ? ".mp3" : ".bin");
                const char* tmpDir = getenv("TMPDIR");
                if (!tmpDir || !tmpDir[0]) tmpDir = "/tmp";
                QString tempPath = QString::fromStdString(std::string(tmpDir) + "/progressive_") +
                    QString::number(std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count()) + suffix;
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

    if (statusLabel_) statusLabel_->setText("Loading full image...");
    ThreadPool::instance().enqueue([guard, client, mxc]() {
        auto r = client->downloadMedia(mxc, 0, 0);
        QImage img;
        if (r.ok && !r.data.empty()) {
            img.loadFromData(r.data.data(), static_cast<int>(r.data.size()));
        }
        QMetaObject::invokeMethod(guard, [guard, img, mxc]() {
            if (guard.isNull()) return;
            if (img.isNull()) {
                QMessageBox::warning(nullptr, "Error", "Failed to load image.");
                return;
            }
            auto* dlg = new ImageViewerDialog(img, QString::fromStdString(mxc), nullptr);
            dlg->exec();
            delete dlg;
        }, Qt::QueuedConnection);
    });
}

} // namespace progressive::desktop
