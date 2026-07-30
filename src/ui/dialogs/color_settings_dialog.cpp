// src/ui/dialogs/color_settings_dialog.cpp
#include "color_settings_dialog.hpp"
#include "../shared/theme.hpp"

#include <QColorDialog>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QScrollArea>
#include <QFrame>

namespace progressive::desktop {

ColorSettingsDialog::ColorSettingsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Colors");
    resize(460, 520);
    auto* root = new QVBoxLayout(this);

    auto* formContainer = new QWidget;
    auto* form = new QFormLayout(formContainer);

    addRow("View background",      &Design::viewBg,         "theme/viewBg");
    addRow("Input background",     &Design::inputBg,        "theme/inputBg");
    addRow("Text",                 &Design::textColor,      "theme/textColor");
    addRow("Muted text",           &Design::mutedTextColor, "theme/mutedTextColor");
    addRow("Dim text",             &Design::dimTextColor,   "theme/dimTextColor");
    addRow("Reaction text",        &Design::reactionTextColor, "theme/reactionTextColor");
    addRow("System text",          &Design::systemTextColor,"theme/systemTextColor");
    addRow("Time",                 &Design::timeColor,      "theme/timeColor");
    addRow("Accent",               &Design::accentColor,    "theme/accentColor");
    addRow("Incoming bubble",      &Design::incomingBubble, "theme/incomingBubble");
    addRow("Outgoing bubble",      &Design::outgoingBubble, "theme/outgoingBubble");
    addRow("Selected bg",          &Design::selectedBg,     "theme/selectedBg");
    addRow("Invite row bg",        &Design::inviteRowBg,    "theme/inviteRowBg");
    addRow("Invite text",          &Design::inviteTextColor,"theme/inviteTextColor");
    addRow("Danger text (red)",    &Design::dangerText,     "theme/dangerText");
    addRow("Danger bg",            &Design::dangerBg,       "theme/dangerBg");
    addRow("Accept bg (green)",    &Design::acceptBg,       "theme/acceptBg");
    addRow("Border",               &Design::borderColor,    "theme/borderColor");
    addRow("Hover border",         &Design::hoverBorder,    "theme/hoverBorder");
    addRow("Pinned color",         &Design::pinnedColor,    "theme/pinnedColor");
    addRow("Thread color",         &Design::threadColor,    "theme/threadColor");
    addRow("Unread badge",         &Design::unreadBadgeColor,"theme/unreadBadgeColor");

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setWidget(formContainer);
    scroll->setFrameShape(QFrame::NoFrame);
    root->addWidget(scroll);

    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    auto* saveBtn = new QPushButton("Apply", this);
    auto* cancelBtn = new QPushButton("Cancel", this);
    btnRow->addWidget(saveBtn);
    btnRow->addWidget(cancelBtn);
    root->addLayout(btnRow);

    connect(saveBtn, &QPushButton::clicked, this, &ColorSettingsDialog::onSave);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
}

void ColorSettingsDialog::addRow(const std::string& label, QColor* target,
                                   const std::string& qsettingsKey) {
    auto* btn = new QPushButton(this);
    btn->setFixedSize(32, 24);
    auto updateBtn = [btn, target]() {
        btn->setStyleSheet(QString("background:%1; border:1px solid " + Design::hoverBorder.name() + ";")
            .arg(target->name()));
    };
    updateBtn();
    btn->setToolTip("Click to change");
    connect(btn, &QPushButton::clicked, this, [target, updateBtn, this]() {
        QColor c = QColorDialog::getColor(*target, this, "Choose color");
        if (c.isValid()) {
            *target = c;
            updateBtn();
        }
    });
    Row r = {qsettingsKey, target, btn};
    rows_.push_back(r);

    auto* row = new QHBoxLayout;
    row->addWidget(btn);
    row->addStretch();

    auto* form = findChild<QFormLayout*>();
    if (form) form->addRow(QString::fromStdString(label), row);
}

void ColorSettingsDialog::onSave() {
    Theme::save();
    accept();
    Theme::reapply();
}

} // namespace progressive::desktop
