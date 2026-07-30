#include "room_switcher_dialog.hpp"
#include "../room_list_model.hpp"

#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>
#include <QKeyEvent>
#include <QApplication>

namespace progressive::desktop {

namespace {
inline constexpr int kSwitcherW = 420;
inline constexpr int kSwitcherH = 360;
} // namespace

RoomSwitcherDialog::RoomSwitcherDialog(RoomListModel* roomModel, QWidget* parent)
    : QDialog(parent), roomModel_(roomModel) {
    setWindowTitle("Switch Room (Ctrl+K)");
    setModal(true);
    resize(kSwitcherW, kSwitcherH);

    filterEdit_ = new QLineEdit(this);
    filterEdit_->setPlaceholderText("Type room name to filter...");
    filterEdit_->installEventFilter(this);

    roomList_ = new QListWidget(this);
    roomList_->setFocusPolicy(Qt::NoFocus);

    auto* root = new QVBoxLayout(this);
    root->addWidget(filterEdit_);
    root->addWidget(roomList_);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    connect(filterEdit_, &QLineEdit::textChanged, this, &RoomSwitcherDialog::filterRooms);
    connect(roomList_, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        if (item) {
            emit roomSelected(item->data(Qt::UserRole).toString());
            accept();
        }
    });

    filterRooms(QString());
    filterEdit_->setFocus();
}

void RoomSwitcherDialog::filterRooms(const QString& text) {
    roomList_->clear();
    int count = roomModel_ ? roomModel_->rowCount() : 0;
    QString lower = text.trimmed().toLower();
    for (int i = 0; i < count; ++i) {
        auto idx = roomModel_->index(i);
        QString name = idx.data(RoomListModel::NameRole).toString();
        QString rid = idx.data(RoomListModel::RoomIdRole).toString();
        bool isInvite = idx.data(RoomListModel::IsInviteRole).toBool();
        if (isInvite) continue;
        if (lower.isEmpty() || name.toLower().contains(lower) || rid.toLower().contains(lower)) {
            auto* item = new QListWidgetItem(name);
            item->setData(Qt::UserRole, rid);
            roomList_->addItem(item);
        }
    }
    if (roomList_->count() > 0) roomList_->setCurrentRow(0);
}

bool RoomSwitcherDialog::eventFilter(QObject* obj, QEvent* event) {
    if (obj == filterEdit_ && event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Up) {
            int row = roomList_->currentRow() - 1;
            if (row >= 0) roomList_->setCurrentRow(row);
            return true;
        }
        if (ke->key() == Qt::Key_Down) {
            int row = roomList_->currentRow() + 1;
            if (row < roomList_->count()) roomList_->setCurrentRow(row);
            return true;
        }
        if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
            auto* cur = roomList_->currentItem();
            if (cur) {
                emit roomSelected(cur->data(Qt::UserRole).toString());
                accept();
            }
            return true;
        }
        if (ke->key() == Qt::Key_Escape) {
            reject();
            return true;
        }
    }
    return QDialog::eventFilter(obj, event);
}

} // namespace progressive::desktop
