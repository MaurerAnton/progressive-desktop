// src/core/crypto/verify_controller.cpp
#include "verify_controller.hpp"
#include "../matrix_client.hpp"
#include "verification.hpp"
#include "../sync_engine.hpp"
#include "../debug_log.hpp"

namespace progressive::desktop {

void VerificationController::sendToDevice(const std::string& eventType,
    const std::string& txnId, const std::string& contentJson,
    const std::string& targetUserId, const std::string& targetDeviceId) {
    if (!client_) return;
    std::ostringstream body;
    body << "{\"messages\":{\"" << targetUserId << "\":{\""
         << targetDeviceId << "\":" << contentJson << "}}}";
    client_->sendToDevice(eventType, txnId, body.str());
    LOG(LogChannel::E2EE, "verifyController: sent %s txn=%s to %s/%s",
        eventType.c_str(), txnId.c_str(), targetUserId.c_str(), targetDeviceId.c_str());
}

void VerificationController::startSelfVerification(
    const std::string& ourUserId, const std::string& ourDeviceId,
    const std::string& otherDeviceId) {
    if (!client_ || !vm_) return;
    auto* txn = vm_->startVerification(ourUserId, otherDeviceId, ourDeviceId);
    if (!txn) return;

    std::string content = vm_->buildRequestContent(ourDeviceId, txn->transactionId);
    sendToDevice("m.key.verification.request", txn->transactionId, content,
                  ourUserId, otherDeviceId);
}

void VerificationController::acceptIncoming(const std::string& txnId) {
    if (!client_ || !vm_) return;
    auto* txn = vm_->findTransaction(txnId);
    if (!txn) return;

    std::string content = vm_->buildReadyContent(txn->ourDeviceId, txnId);
    sendToDevice("m.key.verification.ready", txnId, content,
                  txn->otherUserId, txn->otherDeviceId);

    content = vm_->buildStartContent(txn->ourDeviceId, txnId);
    sendToDevice("m.key.verification.start", txnId, content,
                  txn->otherUserId, txn->otherDeviceId);

    // Responder creates SAS and computes commitment after start
    txn->sas = sasCreate();
}

void VerificationController::confirmMatch(const std::string& txnId) {
    if (!client_ || !vm_) return;
    auto* txn = vm_->findTransaction(txnId);
    if (!txn || !txn->sas.valid) return;

    std::string ourDeviceId = txn->ourDeviceId;

    // Send key
    std::string keyContent = vm_->buildKeyContent(txnId, txn->sas.ourPubkey);
    sendToDevice("m.key.verification.key", txnId, keyContent,
                  txn->otherUserId, txn->otherDeviceId);

    // Compute + send MAC
    std::string macContent = vm_->buildMacContent(*txn, txn->sas);
    sendToDevice("m.key.verification.mac", txnId, macContent,
                  txn->otherUserId, txn->otherDeviceId);

    txn->state = VerificationState::MacSent;
}

void VerificationController::cancelVerification(const std::string& txnId,
    const std::string& reason) {
    if (!client_ || !vm_) return;
    auto* txn = vm_->findTransaction(txnId);
    if (!txn) return;

    std::string content = vm_->buildCancelContent(txnId, CancelCode::User, reason);
    sendToDevice("m.key.verification.cancel", txnId, content,
                  txn->otherUserId, txn->otherDeviceId);
    vm_->removeTransaction(txnId);
}

} // namespace progressive::desktop
