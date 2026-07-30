#include "account_switcher.hpp"
#include "core/matrix_client.hpp"
#include "core/session_store.hpp"
#include "core/sync_engine.hpp"
#include "core/thread_pool.hpp"
#include "core/memory_stats.hpp"
#include "core/crypto/decryptor.hpp"
#include "../room_list_model.hpp"
#include "../timeline/timeline_model.hpp"
#include "../timeline/timeline_delegate.hpp"
#include "../shared/image_loader.hpp"
#include "../chat/chat_view.hpp"
#include "room_handler.hpp"
#include "../dialogs/login_dialog.hpp"
#include <QComboBox>
#include <QLabel>
#include <QWidget>

namespace progressive::desktop {

AccountSwitcher::AccountSwitcher(std::shared_ptr<MatrixClient> client, std::shared_ptr<SessionStore> store, SyncEngine* sync,
                    QComboBox* accountCombo, QLabel* userLabel, QLabel* statusLabel,
                    RoomListModel* roomModel, TimelineModel* timelineModel,
                    ImageLoader* imageLoader, TimelineDelegate* timelineDelegate,
                    RoomHandler* roomHandler, ChatView* chatView,
                    QWidget* placeholder, QWidget* timelineView, QWidget* messageEdit,
                    QObject* parent)
    : QObject(parent), client_(std::move(client)), store_(std::move(store)), sync_(sync),
      accountCombo_(accountCombo), userLabel_(userLabel), statusLabel_(statusLabel),
      roomModel_(roomModel), timelineModel_(timelineModel),
      imageLoader_(imageLoader), timelineDelegate_(timelineDelegate),
      roomHandler_(roomHandler), chatView_(chatView),
      placeholder_(placeholder), timelineView_(timelineView), messageEdit_(messageEdit) {}

void AccountSwitcher::switchAccount(int index) {
    if (index < 0 || !client_ || !store_) return;
    auto accounts = store_->listAccounts();
    if (index >= (int)accounts.size()) return;

    auto& acct = accounts[index];
    if (acct.userId == client_->account().userId) return;

    accountCombo_->setEnabled(false);
    sync_->stop();
    logMemorySnapshot("before-account-switch");

    std::string oldKey = client_->account().userId + "/" + client_->account().deviceId;
    if (sync_->decryptor() && sync_->decryptor()->isInitialized()) {
        auto mp = sync_->decryptor()->megolm()->pickleAll(oldKey);
        if (!mp.empty()) store_->saveMegolmSessions(mp, oldKey);
        auto op = sync_->decryptor()->pickleOlmSessions(oldKey);
        if (!op.empty()) store_->saveOlmSessions(op, oldKey);
        auto obp = sync_->decryptor()->pickleOutboundSessions(oldKey);
        if (!obp.empty() && obp != "[]") store_->saveOutboundSessions(obp, oldKey);
        auto ap = sync_->decryptor()->saveAccountPickle(oldKey);
        if (!ap.empty()) store_->saveOlmAccount(ap, oldKey,
                                                 sync_->decryptor()->accountShared(),
                                                 sync_->decryptor()->account()->uploadedKeyCount());
    }

    roomModel_->clear();
    timelineModel_->clear();
    timelineView_->hide();
    placeholder_->show();
    messageEdit_->hide();
    if (roomHandler_) roomHandler_->clearCurrentRoom();
    if (roomHandler_) roomHandler_->memberAvatarCache().clear();
    chatView_->clear();

    client_->setAccount(acct);

    std::string newKey = acct.userId + "/" + acct.deviceId;
    if (sync_->decryptor()) {
        if (store_) {
            auto saved = store_->loadOlmAccount(newKey);
            if (saved) {
                sync_->decryptor()->init(saved->pickle, saved->pickleKey, saved->shared);
                sync_->decryptor()->account()->setUploadedKeyCount(saved->uploadedKeyCount);
            } else {
                sync_->decryptor()->init();
            }
            auto md = store_->loadMegolmSessions(newKey);
            if (md) sync_->decryptor()->megolm()->unpickleAll(newKey, *md);
            auto od = store_->loadOlmSessions(newKey);
            if (od) sync_->decryptor()->unpickleOlmSessions(newKey, *od);
            auto obd = store_->loadOutboundSessions(newKey);
            if (obd) sync_->decryptor()->unpickleOutboundSessions(newKey, *obd);
        } else {
            sync_->decryptor()->init();
        }
        sync_->setClient(client_);
        sync_->setSessionStore(store_);
        sync_->uploadDeviceKeys();
    }

    userLabel_->setText(" " + QString::fromStdString(acct.userId) + " ");
    timelineDelegate_->setMyUserId(acct.userId);
    imageLoader_->setClient(client_);
    accountCombo_->setCurrentIndex(index);
    accountCombo_->setEnabled(true);

    logMemorySnapshot("after-account-switch");
    sync_->start();
}

void AccountSwitcher::addAccount() {
    if (!client_ || !store_) return;
    sync_->stop();
    accountCombo_->setEnabled(false);

    std::string oldKey = client_->account().userId + "/" + client_->account().deviceId;
    if (sync_->decryptor() && sync_->decryptor()->isInitialized()) {
        auto mp = sync_->decryptor()->megolm()->pickleAll(oldKey);
        if (!mp.empty()) store_->saveMegolmSessions(mp, oldKey);
        auto op = sync_->decryptor()->pickleOlmSessions(oldKey);
        if (!op.empty()) store_->saveOlmSessions(op, oldKey);
        auto obp = sync_->decryptor()->pickleOutboundSessions(oldKey);
        if (!obp.empty() && obp != "[]") store_->saveOutboundSessions(obp, oldKey);
        auto ap = sync_->decryptor()->saveAccountPickle(oldKey);
        if (!ap.empty()) store_->saveOlmAccount(ap, oldKey,
                                                 sync_->decryptor()->accountShared(),
                                                 sync_->decryptor()->account()->uploadedKeyCount());
    }

    auto* parentWidget = qobject_cast<QWidget*>(parent());
    LoginDialog dlg(client_.get(), store_.get(), parentWidget);
    if (dlg.exec() == QDialog::Accepted && dlg.loggedIn()) {
        roomModel_->clear();
        timelineModel_->clear();
        timelineView_->hide();
        placeholder_->show();
        messageEdit_->hide();
        if (roomHandler_) roomHandler_->clearCurrentRoom();
        if (roomHandler_) roomHandler_->memberAvatarCache().clear();
        chatView_->clear();

        accountCombo_->blockSignals(true);
        accountCombo_->clear();
        auto accounts = store_->listAccounts();
        int targetIdx = -1;
        for (size_t i = 0; i < accounts.size(); i++) {
            accountCombo_->addItem(QString::fromStdString(accounts[i].userId));
            if (accounts[i].userId == client_->account().userId) targetIdx = (int)i;
        }
        accountCombo_->insertSeparator(accountCombo_->count());
        accountCombo_->addItem("+ Add Account");
        accountCombo_->addItem("Logout");
        if (targetIdx >= 0) accountCombo_->setCurrentIndex(targetIdx);
        accountCombo_->blockSignals(false);

        sync_->decryptor()->init();
        sync_->setClient(client_);
        sync_->setSessionStore(store_);
        sync_->uploadDeviceKeys();

        userLabel_->setText(" " + QString::fromStdString(client_->account().userId) + " ");
        timelineDelegate_->setMyUserId(client_->account().userId);
        imageLoader_->setClient(client_);
        accountCombo_->setEnabled(true);
        sync_->start();
    } else {
        accountCombo_->setEnabled(true);
        sync_->start();
    }
}

int AccountSwitcher::accountCount() {
    auto accounts = store_->listAccounts();
    return (int)accounts.size();
}

int AccountSwitcher::currentAccountIndex() {
    auto accounts = store_->listAccounts();
    for (size_t i = 0; i < accounts.size(); i++) {
        if (accounts[i].userId == client_->account().userId) return (int)i;
    }
    return 0;
}

} // namespace progressive::desktop
