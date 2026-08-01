// src/ui/dialogs/sas_verification_dialog.cpp
#include "sas_verification_dialog.hpp"
#include "core/crypto/sas_emojis.hpp"
#include "core/debug_log.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFrame>

namespace progressive::desktop {

SasVerificationDialog::SasVerificationDialog(const std::string& txnId,
    const std::string& otherUser, const std::vector<VerificationEmoji>& emojis,
    QWidget* parent)
    : QDialog(parent), txnId_(txnId) {
    setWindowTitle("Verify " + QString::fromStdString(otherUser));

    auto* layout = new QVBoxLayout(this);

    auto* intro = new QLabel(
        "Compare these emojis with " + QString::fromStdString(otherUser) +
        " — they must appear in the same order on both devices.", this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    emojiLabel_ = new QLabel(QString::fromStdString(formatSasEmojis(emojis)), this);
    QFont emojiFont = emojiLabel_->font();
    emojiFont.setPointSize(20);
    emojiLabel_->setFont(emojiFont);
    emojiLabel_->setAlignment(Qt::AlignCenter);
    emojiLabel_->setWordWrap(true);
    layout->addWidget(emojiLabel_);

    statusLabel_ = new QLabel("Waiting for your comparison…", this);
    statusLabel_->setWordWrap(true);
    statusLabel_->setAlignment(Qt::AlignCenter);
    layout->addWidget(statusLabel_);

    auto* btnRow = new QHBoxLayout;
    matchBtn_ = new QPushButton("They match", this);
    noMatchBtn_ = new QPushButton("They don't match", this);
    cancelBtn_ = new QPushButton("Cancel", this);
    btnRow->addWidget(matchBtn_);
    btnRow->addWidget(noMatchBtn_);
    btnRow->addWidget(cancelBtn_);
    layout->addLayout(btnRow);

    connect(matchBtn_, &QPushButton::clicked, this, [this]() {
        matchBtn_->setEnabled(false);
        noMatchBtn_->setEnabled(false);
        cancelBtn_->setEnabled(false);
        setStatus("SAS matched — sending MAC…");
        emit matched();
    });
    connect(noMatchBtn_, &QPushButton::clicked, this, [this]() {
        emit mismatched();
    });
    connect(cancelBtn_, &QPushButton::clicked, this, [this]() {
        emit cancelled();
    });

    setMinimumWidth(360);
}

void SasVerificationDialog::setStatus(const QString& text) {
    if (statusLabel_) statusLabel_->setText(text);
}

} // namespace progressive::desktop
