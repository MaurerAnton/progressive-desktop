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
        if (!mp.empty()) store_->saveMegolmSessions(mp);
        auto op = sync_->decryptor()->pickleOlmSessions(oldKey);
        if (!op.empty()) store_->saveOlmSessions(op);
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
    if (sync_->decryptor() && sync_->decryptor()->init()) {
        if (store_) {
            auto saved = store_->loadOlmAccount();
            if (saved) sync_->decryptor()->init(saved->first, saved->second);
            auto md = store_->loadMegolmSessions();
            if (md) sync_->decryptor()->megolm()->unpickleAll(newKey, *md);
            auto od = store_->loadOlmSessions();
            if (od) sync_->decryptor()->unpickleOlmSessions(newKey, *od);
        }
        sync_->uploadDeviceKeys();
    }

    userLabel_->setText(" " + QString::fromStdString(acct.userId) + " ");
    timelineDelegate_->setMyUserId(acct.userId);
    imageLoader_->setClient(client_);
    accountCombo_->setCurrentIndex(index);
    accountCombo_->setEnabled(true);

    sync_->setClient(client_);
    sync_->setSessionStore(store_);
    logMemorySnapshot("after-account-switch");
    sync_->start();
}

} // namespace progressive::desktop
