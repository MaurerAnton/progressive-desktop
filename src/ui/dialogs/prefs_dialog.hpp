// src/ui/prefs_dialog.hpp — user preferences dialog.
#pragma once
#include <QDialog>
#include <QSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QSettings>
#include <functional>
#include <memory>

class QVBoxLayout;

namespace progressive::desktop {

class MatrixClient;
class VerificationHandler;
class Decryptor;

class PrefsDialog : public QDialog {
    Q_OBJECT
public:
    explicit PrefsDialog(QWidget* parent = nullptr);

    void setClient(std::shared_ptr<MatrixClient> c) { client_ = std::move(c); }
    void setVerificationHandler(VerificationHandler* vh) { verifyHandler_ = vh; }
    void setDecryptor(Decryptor* d) { decryptor_ = d; }
    using SetupCrossSigningFn = std::function<bool()>;
    using SetupCrossSigningWithPasswordFn = std::function<bool(const std::string&)>;
    using UiaSessionFn = std::function<std::string()>;
    void setSetupCrossSigningFn(SetupCrossSigningFn fn) { setupCrossSigningFn_ = std::move(fn); }
    void setSetupCrossSigningWithPasswordFn(SetupCrossSigningWithPasswordFn fn) { setupCrossSigningWithPasswordFn_ = std::move(fn); }
    void setUiaSessionFn(UiaSessionFn fn) { uiaSessionFn_ = std::move(fn); }

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

    static bool shareKeysVerifiedOnly() {
        QSettings s;
        return s.value("e2ee/shareKeysVerifiedOnly", false).toBool();
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
    void loadDevices(QVBoxLayout* sectionLayout);

    std::shared_ptr<MatrixClient> client_;
    VerificationHandler* verifyHandler_ = nullptr;
    Decryptor* decryptor_ = nullptr;
    SetupCrossSigningFn setupCrossSigningFn_;
    SetupCrossSigningWithPasswordFn setupCrossSigningWithPasswordFn_;
    UiaSessionFn uiaSessionFn_;
    QSpinBox* cacheSpin_;
    QSpinBox* syncTimeoutSpin_ = nullptr;
    QSpinBox* historySpin_ = nullptr;
    QCheckBox* invisibleCheck_ = nullptr;
    QCheckBox* verifiedOnlyCheck_ = nullptr;
    QLabel* xsStatusLabel_ = nullptr;
};

} // namespace progressive::desktop
