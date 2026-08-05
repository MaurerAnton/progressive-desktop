// src/ui/handlers/verification_handler.cpp
#include "verification_handler.hpp"
#include "../dialogs/sas_verification_dialog.hpp"
#include "core/matrix_client.hpp"
#include "core/sync_engine.hpp"
#include "core/crypto/verification.hpp"
#include "core/crypto/verify_controller.hpp"
#include "core/debug_log.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStatusBar>
#include <QWidget>

namespace progressive::desktop {

VerificationHandler::VerificationHandler(QWidget* parent)
    : QObject(parent) {
    banner_ = new QWidget(parent);
    auto* bannerLayout = new QHBoxLayout(banner_);
    bannerLayout->setContentsMargins(4, 2, 4, 2);
    bannerLabel_ = new QLabel(banner_);
    acceptBtn_ = new QPushButton("Accept", banner_);
    rejectBtn_ = new QPushButton("Reject", banner_);
    bannerLayout->addWidget(bannerLabel_, 1);
    bannerLayout->addWidget(acceptBtn_);
    bannerLayout->addWidget(rejectBtn_);
    banner_->hide();

    connect(acceptBtn_, &QPushButton::clicked, this, [this]() {
        if (!bannerTxnId_.empty()) {
            controller_.acceptIncoming(bannerTxnId_);
            hideBanner();
        }
    });
    connect(rejectBtn_, &QPushButton::clicked, this, [this]() {
        if (!bannerTxnId_.empty()) {
            controller_.cancelVerification(bannerTxnId_, CancelCode::User);
            hideBanner();
        }
    });
}

void VerificationHandler::setClient(std::shared_ptr<MatrixClient> c) {
    client_ = std::move(c);
    controller_.setClient(client_);
}

void VerificationHandler::setSyncEngine(SyncEngine* sync) {
    if (!sync) return;
    vm_ = &sync->verificationManager();
    controller_.setVerificationManager(vm_);
    controller_.setSyncEngine(sync);  // enables Olm-wrapped verification sends
    vm_->setStateChangedFn([this, vm = vm_](VerificationTransaction* txn) {
        std::string txnId = txn ? txn->transactionId : "";
        QMetaObject::invokeMethod(this, [this, vm, txnId]() {
            if (txnId.empty() || !vm) return;
            auto* t = vm->findTransaction(txnId);
            if (t) onTransactionStateChanged(t);
        }, Qt::QueuedConnection);
    });
}

void VerificationHandler::startSelfVerification(const std::string& otherDeviceId) {
    if (!client_ || !vm_) return;
    const auto& acc = client_->account();
    controller_.startSelfVerification(acc.userId, acc.deviceId, otherDeviceId);
}

void VerificationHandler::startUserVerification(const std::string& userId,
    const std::string& deviceId) {
    if (!client_ || !vm_) return;
    controller_.startUserVerification(userId, deviceId);
}

void VerificationHandler::onTransactionStateChanged(VerificationTransaction* txn) {
    if (!txn) return;
    switch (txn->state) {
        case VerificationState::RequestReceived:
            showBanner(txn->transactionId, txn->otherUserId);
            break;
        case VerificationState::KeyReceived: {
            auto emojis = vm_ ? vm_->computeEmojis(*txn) : std::vector<VerificationEmoji>{};
            if (!emojis.empty())
                showEmojiDialog(txn);
            break;
        }
        case VerificationState::Done:
            closeDialog();
            hideBanner();
            LOG(LogChannel::E2EE, "verifyHandler: txn %s DONE",
                txn->transactionId.c_str());
            resumeStalledDialog();
            break;
        case VerificationState::Cancelled:
            closeDialog();
            hideBanner();
            LOG(LogChannel::E2EE, "verifyHandler: txn %s CANCELLED code=%s",
                txn->transactionId.c_str(),
                cancelCodeToString(txn->cancelCode.value_or(CancelCode::Other)).c_str());
            resumeStalledDialog();
            break;
        default:
            break;
    }
}

void VerificationHandler::showBanner(const std::string& txnId,
    const std::string& fromUser) {
    bannerTxnId_ = txnId;
    bannerLabel_->setText(QString("%1 wants to verify your device").arg(
        QString::fromStdString(fromUser)));
    banner_->show();
}

void VerificationHandler::hideBanner() {
    bannerTxnId_.clear();
    banner_->hide();
}

void VerificationHandler::showEmojiDialog(VerificationTransaction* txn) {
    if (!vm_ || dialog_) return;  // one dialog at a time
    auto emojis = vm_->computeEmojis(*txn);
    if (emojis.empty()) return;

    dialogTxnId_ = txn->transactionId;
    QWidget* parentWidget = qobject_cast<QWidget*>(parent());
    dialog_ = new SasVerificationDialog(dialogTxnId_, txn->otherUserId, emojis, parentWidget);
    dialog_->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog_, &SasVerificationDialog::matched, this, [this]() {
        if (!dialogTxnId_.empty())
            controller_.confirmMatch(dialogTxnId_);
    });
    connect(dialog_, &SasVerificationDialog::mismatched, this, [this]() {
        if (!dialogTxnId_.empty())
            controller_.cancelVerification(dialogTxnId_, CancelCode::Sasmismatch);
        closeDialog();
    });
    connect(dialog_, &SasVerificationDialog::cancelled, this, [this]() {
        if (!dialogTxnId_.empty())
            controller_.cancelVerification(dialogTxnId_, CancelCode::User);
        closeDialog();
    });
    dialog_->show();
}

// If a concurrent verification is waiting at KeyReceived (its dialog was
// skipped by the one-dialog guard), surface it now that this dialog is closed.
void VerificationHandler::resumeStalledDialog() {
    if (!vm_ || dialog_) return;
    for (auto* t : vm_->activeTransactions()) {
        if (t->state == VerificationState::KeyReceived) {
            showEmojiDialog(t);
            break;
        }
    }
}

void VerificationHandler::closeDialog() {
    if (dialog_) dialog_->close();
    dialog_ = nullptr;
    dialogTxnId_.clear();
}

} // namespace progressive::desktop
