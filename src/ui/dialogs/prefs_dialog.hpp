// src/ui/prefs_dialog.hpp — user preferences dialog.
#pragma once
#include <QDialog>
#include <QSpinBox>
#include <QCheckBox>
#include <QSettings>

namespace progressive::desktop {

class PrefsDialog : public QDialog {
    Q_OBJECT
public:
    explicit PrefsDialog(QWidget* parent = nullptr);

    static int imageCacheSize() {
        QSettings s;
        return s.value("cache/imageCount", 20).toInt();
    }

    static int pollTimeoutMs() {
        QSettings s;
        return s.value("sync/pollTimeoutMs", 3000).toInt();
    }

    static int historyLoadLimit() {
        QSettings s;
        return s.value("sync/historyLoadLimit", 50).toInt();
    }

    static bool invisibleMode() {
        QSettings s;
        return s.value("privacy/invisibleMode", false).toBool();
    }

private slots:
    void onSave();

signals:
    void settingsChanged();

private:
    QSpinBox* cacheSpin_;
    QSpinBox* syncTimeoutSpin_ = nullptr;
    QSpinBox* historySpin_ = nullptr;
    QCheckBox* invisibleCheck_ = nullptr;
};

} // namespace progressive::desktop
