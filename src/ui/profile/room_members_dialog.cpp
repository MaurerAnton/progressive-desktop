// src/ui/room_members_dialog.cpp
#include "room_members_dialog.hpp"
#include "../timeline/timeline_model.hpp"
#include <QHBoxLayout>
#include <set>
#include "core/crypto/cross_sign.hpp"
#include "../shared/theme.hpp"
#include <QPointer>
#include "user_profile_dialog.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMetaObject>
#include <QPointer>
#include <QMessageBox>
#include <QMenu>
#include <QThread>
#include "core/thread_pool.hpp"

#include <simdjson.h>

namespace progressive::desktop {
namespace {
inline constexpr int kDialogW = 400;
inline constexpr int kDialogH = 300;
} // namespace

RoomMembersDialog::RoomMembersDialog(MatrixClient* client, const std::string& roomId,
                                       QWidget* parent)
    : QDialog(parent), client_(client), roomId_(roomId) {
    setWindowTitle("Room Members");
    setModal(true);
    resize(kDialogW, kDialogH);

    searchEdit_ = new QLineEdit(this);
    searchEdit_->setPlaceholderText("Search members...");
    searchEdit_->setStyleSheet("padding:6px; background:" + Design::accountComboBg.name() + "; border:1px solid " + Design::borderColor.name() + "; color:" + Design::logViewText.name() + "; border-radius:4px;");

    list_ = new QListWidget(this);
    list_->setStyleSheet("QListWidget{background:" + Design::inputBg.name() + "; border:1px solid " + Design::borderColor.name() + ";} "
                         "QListWidget::item{color:" + Design::logViewText.name() + "; padding:6px;} "
                         "QListWidget::item:hover{background:" + Design::incomingBubble.name() + ";}");
    Theme::addListener([guard = QPointer<QListWidget>(list_)]() {
        if (!guard) return;
        guard->setStyleSheet("QListWidget{background:" + Design::inputBg.name() + "; border:1px solid " + Design::borderColor.name() + ";} "
                             "QListWidget::item{color:" + Design::logViewText.name() + "; padding:6px;} "
                             "QListWidget::item:hover{background:" + Design::incomingBubble.name() + ";}");
    });

    statusLabel_ = new QLabel("Loading members...", this);
    statusLabel_->setStyleSheet("color:" + Design::mutedTextColor.name() + ";");
    closeBtn_ = new QPushButton("Close", this);

    auto* root = new QVBoxLayout(this);
    root->addWidget(searchEdit_);
    root->addWidget(list_);
    root->addWidget(statusLabel_);
    auto* btnRow = new QHBoxLayout;
    reloadBtn_ = new QPushButton("Reload", this);
    reloadBtn_->setEnabled(false);
    btnRow->addWidget(reloadBtn_);
    btnRow->addStretch();
    btnRow->addWidget(closeBtn_);
    root->addLayout(btnRow);
    connect(reloadBtn_, &QPushButton::clicked, this, &RoomMembersDialog::reload);

    // 150ms debounce: filter client-side, not reload from server
    debounceTimer_ = new QTimer(this);
    debounceTimer_->setSingleShot(true);
    debounceTimer_->setInterval(150);
    connect(searchEdit_, &QLineEdit::textChanged, this, [this]() {
        if (loaded_) debounceTimer_->start();
    });
    connect(debounceTimer_, &QTimer::timeout, this, &RoomMembersDialog::applyFilter);
    connect(list_, &QListWidget::itemClicked, this, &RoomMembersDialog::onMemberClicked);
    connect(list_, &QListWidget::customContextMenuRequested, this, &RoomMembersDialog::onMemberContextMenu);
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(closeBtn_, &QPushButton::clicked, this, &QDialog::accept);

    loadMembers();
}

void RoomMembersDialog::loadMembers() {
    statusLabel_->setText("Loading members...");
    list_->clear();

    QPointer<RoomMembersDialog> guard(this);

    ThreadPool::instance().enqueue([guard, this]() {
        auto r = client_->getRoomMembers(roomId_);
        std::vector<MemberInfo> members;

        if (r.ok) {
            simdjson::dom::parser parser;
            auto root = parser.parse(r.data);
            if (root.error() == simdjson::SUCCESS) {
                auto chunk = root.value()["chunk"].get_array();
                if (chunk.error() == simdjson::SUCCESS) {
                    for (auto evt : chunk.value()) {
                        auto content = evt["content"];
                        if (content.error() != simdjson::SUCCESS) continue;

                        auto membership = content.value()["membership"].get_string();
                        if (membership.error() != simdjson::SUCCESS) continue;
                        std::string memberStr(membership.value());
                        if (memberStr != "join") continue;

                        MemberInfo m;
                        m.membership = memberStr;

                        auto sk = evt["state_key"].get_string();
                        if (sk.error() == simdjson::SUCCESS) m.userId = std::string(sk.value());

                        // Use simdjson directly — no manual string search
                        auto dn = content.value()["displayname"].get_string();
                        if (dn.error() == simdjson::SUCCESS)
                            m.displayName = std::string(dn.value());

                        auto av = content.value()["avatar_url"].get_string();
                        if (av.error() == simdjson::SUCCESS)
                            m.avatarUrl = std::string(av.value());

                        members.push_back(std::move(m));
                    }
                }
            }
        }

        // Trust shields: one batch /keys/query for all members, then per-user
        // trust (0 unverified, 1 SSK cross-signed, 2 SAS-verified).
        std::map<std::string, int> trust;
        {
            std::string queryBody = "{\"device_keys\":{";
            bool firstUser = true;
            for (const auto& mbr : members) {
                if (!firstUser) queryBody += ",";
                firstUser = false;
                queryBody += "\"" + mbr.userId + "\":[]";
            }
            queryBody += "}}";
            auto q = client_->queryKeys(queryBody);
            if (q.ok) {
                std::string ourUsk = store_ ? store_->loadUserSigningPub(client_->account().userId) : "";
                std::string ourUid = client_->account().userId;
                for (const auto& mbr : members) {
                    int level = 0;
                    auto trustRes = computeDeviceTrust(q.data, mbr.userId, ourUid, ourUsk);
                    for (const auto& r : trustRes) {
                        if (r.trust == DeviceTrust::Verified) level = 2;
                        else if (r.trust == DeviceTrust::Trusted && level < 2) level = 1;
                        if (store_ && store_->isDeviceVerified(mbr.userId, r.deviceId)) level = 2;
                    }
                    trust[mbr.userId] = level;
                }
            }
        }

        bool ok = r.ok;
        QString failReason = ok ? QString()
            : QString("HTTP %1 %2").arg(r.httpStatus)
                  .arg(QString::fromStdString(r.error.message));
        QMetaObject::invokeMethod(guard, [guard, members = std::move(members), ok,
                                         trust = std::move(trust),
                                         failReason = std::move(failReason)]() {
            if (guard.isNull()) return;
            guard->allMembers_ = std::move(members);
            guard->userTrust_ = std::move(trust);
            guard->loaded_ = true;
            QString status;
            if (guard->allMembers_.empty() && !ok) {
                // Local-state fallback: the members seen in the current
                // timeline (a failed /members shouldn't yield an empty list).
                std::set<std::string> seen;
                if (guard->fallbackModel_) {
                    for (int i = 0; i < guard->fallbackModel_->rowCount(); ++i) {
                        auto* evt = guard->fallbackModel_->at(i);
                        if (!evt || evt->senderId.empty() || !seen.insert(evt->senderId).second)
                            continue;
                        MemberInfo m;
                        m.userId = evt->senderId;
                        m.displayName = evt->senderName;
                        m.avatarUrl = evt->avatarUrl;
                        m.membership = "join";
                        guard->allMembers_.push_back(std::move(m));
                    }
                }
                status = guard->allMembers_.empty()
                    ? "Failed to load members: " + failReason
                    : QString("Server fetch failed (%1) — showing %2 local members")
                          .arg(failReason).arg(guard->allMembers_.size());
            } else if (guard->allMembers_.empty()) {
                status = "No members found";
            } else {
                status = QString("%1 members").arg(guard->allMembers_.size());
            }
            guard->statusLabel_->setText(status);
            guard->reloadBtn_->setEnabled(true);
            guard->applyFilter();
        }, Qt::QueuedConnection);
    });
}

void RoomMembersDialog::reload() {
    loaded_ = false;
    list_->clear();
    statusLabel_->setText("Loading members...");
    loadMembers();
}

void RoomMembersDialog::applyFilter() {
    QString search = searchEdit_->text().trimmed().toLower();
    list_->clear();

    for (const auto& m : allMembers_) {
        QString display = QString::fromStdString(m.displayName.empty() ? m.userId : m.displayName);
        if (!search.isEmpty()) {
            if (!display.toLower().contains(search) &&
                !QString::fromStdString(m.userId).toLower().contains(search))
                continue;
        }
        int level = 0;
        auto tit = userTrust_.find(m.userId);
        if (tit != userTrust_.end()) level = tit->second;
        QString shieldColor = level == 2 ? "#4CAF50" : (level == 1 ? "#9E9E9E" : "#F44336");
        display = "<span style='color:" + shieldColor + ";'>●</span> " + display;
        auto* item = new QListWidgetItem(display);
        item->setData(Qt::UserRole, QString::fromStdString(m.userId));
        item->setToolTip(QString::fromStdString(m.userId));
        list_->addItem(item);
    }
}

void RoomMembersDialog::onSearchChanged() {
    // Handled by debounceTimer_ → applyFilter()
}

void RoomMembersDialog::onMemberClicked(QListWidgetItem* item) {
    if (!item) return;
    QString userId = item->data(Qt::UserRole).toString();
    UserProfileDialog dlg(client_, roomId_, userId.toStdString(), this);
    connect(&dlg, &UserProfileDialog::verifyRequested, this,
            &RoomMembersDialog::verifyRequested);
    dlg.exec();
}

void RoomMembersDialog::onMemberContextMenu(const QPoint& pos) {
    auto* item = list_->itemAt(pos);
    if (!item) return;
    QString userId = item->data(Qt::UserRole).toString();

    QMenu menu(this);
    QAction* verifyAction = menu.addAction("Verify…");
    QAction* chosen = menu.exec(list_->mapToGlobal(pos));
    if (chosen == verifyAction)
        startVerifyForUser(userId);
}

void RoomMembersDialog::startVerifyForUser(const QString& userId) {
    if (!client_ || !client_->isLoggedIn()) return;
    const std::string ourDeviceId = client_->account().deviceId;

    std::string queryBody = "{\"device_keys\":{\"" + userId.toStdString() + "\":[]}}";
    auto resp = client_->queryKeys(queryBody);
    if (!resp.ok) {
        QMessageBox::information(this, "Verify",
            QString("Could not fetch devices for %1.").arg(userId));
        return;
    }

    simdjson::dom::parser p;
    auto doc = p.parse(resp.data);
    if (doc.error() != simdjson::SUCCESS) {
        QMessageBox::information(this, "Verify", "Could not parse device keys.");
        return;
    }
    auto userObj = doc.value()["device_keys"][userId.toStdString()];
    if (userObj.error() != simdjson::SUCCESS) {
        QMessageBox::information(this, "Verify",
            QString("%1 has no devices to verify.").arg(userId));
        return;
    }

    for (auto dev : userObj.value().get_object().value()) {
        std::string deviceId(dev.key);
        if (deviceId == ourDeviceId) continue;  // don't verify our own device
        emit verifyRequested(userId, QString::fromStdString(deviceId));
        return;
    }
    QMessageBox::information(this, "Verify", "No other devices found to verify.");
}

} // namespace progressive::desktop
