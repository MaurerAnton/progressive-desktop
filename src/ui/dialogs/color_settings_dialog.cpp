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

    addRow("View background",      &Design::viewBg,         "theme/viewBg", form);
    addRow("Input background",     &Design::inputBg,        "theme/inputBg", form);
    addRow("Text",                 &Design::textColor,      "theme/textColor", form);
    addRow("Muted text",           &Design::mutedTextColor, "theme/mutedTextColor", form);
    addRow("Dim text",             &Design::dimTextColor,   "theme/dimTextColor", form);
    addRow("Reaction text",        &Design::reactionTextColor, "theme/reactionTextColor", form);
    addRow("System text",          &Design::systemTextColor,"theme/systemTextColor", form);
    addRow("Time",                 &Design::timeColor,      "theme/timeColor", form);
    addRow("Accent",               &Design::accentColor,    "theme/accentColor", form);
    addRow("Incoming bubble",      &Design::incomingBubble, "theme/incomingBubble", form);
    addRow("Outgoing bubble",      &Design::outgoingBubble, "theme/outgoingBubble", form);
    addRow("Selected bg",          &Design::selectedBg,     "theme/selectedBg", form);
    addRow("Invite row bg",        &Design::inviteRowBg,    "theme/inviteRowBg", form);
    addRow("Invite text",          &Design::inviteTextColor,"theme/inviteTextColor", form);
    addRow("Danger text (red)",    &Design::dangerText,     "theme/dangerText", form);
    addRow("Danger bg",            &Design::dangerBg,       "theme/dangerBg", form);
    addRow("Accept bg (green)",    &Design::acceptBg,       "theme/acceptBg", form);
    addRow("Border",               &Design::borderColor,    "theme/borderColor", form);
    addRow("Hover border",         &Design::hoverBorder,    "theme/hoverBorder", form);
    addRow("Pinned color",         &Design::pinnedColor,    "theme/pinnedColor", form);
    addRow("Thread color",         &Design::threadColor,    "theme/threadColor", form);
    addRow("Unread badge",         &Design::unreadBadgeColor,"theme/unreadBadgeColor", form);

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
                                   const std::string& qsettingsKey, QFormLayout* form) {
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

    form->addRow(QString::fromStdString(label), row);
}

void ColorSettingsDialog::onSave() {
    Theme::save();
    accept();
    Theme::reapply();
}

} // namespace progressive::desktop
