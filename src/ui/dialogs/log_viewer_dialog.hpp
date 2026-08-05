// src/ui/dialogs/log_viewer_dialog.hpp
#pragma once

#include <QDialog>
#include <QListWidget>
#include <QTimer>
#include <memory>
#include <set>
#include <vector>

class QCheckBox;
class QLineEdit;
class QPushButton;

namespace progressive::desktop {

// In-app log viewer: shows the LOG() ring buffer (all channels) with
// per-channel toggles and a text filter — no console needed.
class LogViewerDialog : public QDialog {
    Q_OBJECT
public:
    explicit LogViewerDialog(QWidget* parent = nullptr);
private:
    void refresh();
    void rebuild();
    void onCopy();

    QListWidget* list_ = nullptr;
    QLineEdit* filterEdit_ = nullptr;
    QPushButton* copyBtn_ = nullptr;
    std::vector<QCheckBox*> channelBoxes_;
    QTimer timer_;
    std::set<uint64_t> shownSeqs_;  // seqs already in the list (dedup)
};

}  // namespace progressive::desktop
