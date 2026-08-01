// src/ui/room_members_dialog.cpp
#include "room_members_dialog.hpp"
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
    root->addWidget(closeBtn_);

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

        QMetaObject::invokeMethod(guard, [guard, members = std::move(members), ok = r.ok]() {
            if (guard.isNull()) return;
            guard->allMembers_ = std::move(members);
            guard->loaded_ = true;
            guard->statusLabel_->setText(
                guard->allMembers_.empty()
                    ? (ok ? "No members found" : "Failed to load members — try again")
                    : QString("%1 members").arg(guard->allMembers_.size()));
            guard->applyFilter();
        }, Qt::QueuedConnection);
    });
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
