// src/ui/handlers/verification_handler.hpp — drives SAS verification UI.
#pragma once
#include <QObject>
#include <QPointer>
#include <QString>
#include <memory>
#include <string>
#include "core/crypto/verify_controller.hpp"

class QLabel;
class QPushButton;
class QWidget;

namespace progressive::desktop {

class MatrixClient;
class SyncEngine;
class VerificationManager;
class VerificationController;
class SasVerificationDialog;
class VerificationTransaction;

class VerificationHandler : public QObject {
    Q_OBJECT
public:
    explicit VerificationHandler(QWidget* parent = nullptr);

    void setClient(std::shared_ptr<MatrixClient> c);
    void setSyncEngine(SyncEngine* sync);

    void startSelfVerification(const std::string& otherDeviceId);
    void startUserVerification(const std::string& userId, const std::string& deviceId);

    QWidget* bannerWidget() const { return banner_; }

private:
    void onTransactionStateChanged(VerificationTransaction* txn);
    void showBanner(const std::string& txnId, const std::string& fromUser);
    void hideBanner();
    void showEmojiDialog(VerificationTransaction* txn);
    void closeDialog();
    void resumeStalledDialog();

    std::shared_ptr<MatrixClient> client_;
    VerificationManager* vm_ = nullptr;
    VerificationController controller_;

    QWidget* banner_ = nullptr;
    QLabel* bannerLabel_ = nullptr;
    QPushButton* acceptBtn_ = nullptr;
    QPushButton* rejectBtn_ = nullptr;
    std::string bannerTxnId_;

    QPointer<SasVerificationDialog> dialog_;
    std::string dialogTxnId_;
};

} // namespace progressive::desktop
