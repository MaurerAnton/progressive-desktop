// src/ui/emoji_picker.cpp
#include "emoji_picker.hpp"

#include <QVBoxLayout>
#include <QLabel>

namespace progressive::desktop {

namespace {
// Common Unicode emojis — enough for reactions.
// Each is a single QString (some are multi-codepoint).
const QVector<QString> COMMON_EMOJIS = {
    "😀","😃","😄","😁","😆","😅","😂","🤣","😊","😇",
    "🙂","🙃","😉","😌","😍","🥰","😘","😗","😙","😚",
    "😋","😛","😝","😜","🤪","🤨","🧐","🤓","😎","🥸",
    "🤩","🥳","😏","😒","😞","😔","😟","😕","🙁","☹️",
    "😣","😖","😫","😩","🥺","😢","😭","😤","😠","😡",
    "🤬","🤯","😳","🥵","🥶","😱","😨","😰","😥","😓",
    "🤗","🤔","🤭","🤫","🤥","😶","😐","😑","😬","🙄",
    "😯","😦","😧","😮","😲","🥱","😴","🤤","😪","😵",
    "🤐","🥴","🤢","🤮","🤧","😷","🤒","🤕","🤑","🤠",
    "👍","👎","👌","✌️","🤞","🤟","🤘","🤙","👈","👉",
    "👆","🖕","👇","☝️","👋","🤚","🖐️","✋","🖖","👏",
    "🙌","🤝","🙏","✍️","💪","🦾","🦿","🦵","🦶","👂",
    "❤️","🧡","💛","💚","💙","💜","🖤","🤍","🤎","💔",
    "❣️","💕","💞","💓","💗","💖","💘","💝","💟","❌",
    "⭕","✅","❎","🆗","🆒","🆕","🆙","🅰️","🅱️","🆎",
    "🔥","⭐","🌟","✨","⚡","💥","💫","💦","💨","空洞",
    "🎉","🎊","🎁","🎂","🍰","🍔","🍟","🍕","🌭","🍿",
    "🥂","🍻","🍷","🍸","🍹","🍺","🥃","☕","🍵","🥤",
};
}

EmojiPicker::EmojiPicker(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Pick emoji");
    setModal(true);
    resize(400, 300);

    searchEdit_ = new QLineEdit(this);
    searchEdit_->setPlaceholderText("Search emoji...");

    grid_ = new QGridLayout;
    grid_->setSpacing(2);

    auto* root = new QVBoxLayout(this);
    root->addWidget(searchEdit_);
    auto* scrollContent = new QWidget;
    scrollContent->setLayout(grid_);
    root->addWidget(scrollContent);

    populateEmojis();
    buildGrid();

    connect(searchEdit_, &QLineEdit::textChanged, this, &EmojiPicker::onSearchChanged);
}

void EmojiPicker::populateEmojis() {
    allEmojis_ = COMMON_EMOJIS;
    filteredEmojis_ = allEmojis_;
}

void EmojiPicker::buildGrid() {
    // Clear existing
    QLayoutItem* item;
    while ((item = grid_->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }

    int cols = 10;
    for (int i = 0; i < filteredEmojis_.size(); ++i) {
        auto* btn = new QPushButton(filteredEmojis_[i], this);
        btn->setFixedSize(32, 32);
        btn->setToolTip(filteredEmojis_[i]);
        grid_->addWidget(btn, i / cols, i % cols);
        connect(btn, &QPushButton::clicked, this, [this, emoji = filteredEmojis_[i]]() {
            onEmojiClicked(emoji);
        });
    }
}

void EmojiPicker::onEmojiClicked(const QString& emoji) {
    emit emojiSelected(emoji);
    accept();
}

void EmojiPicker::onSearchChanged(const QString& text) {
    // Simple filter — in a real implementation we'd have emoji names/keywords.
    // For now, just show all if search is empty.
    if (text.isEmpty()) {
        filteredEmojis_ = allEmojis_;
    } else {
        // No name DB yet — show all (the search will be useful when we add
        // the progressive::emoji_search module integration).
        filteredEmojis_ = allEmojis_;
    }
    buildGrid();
}

} // namespace progressive::desktop
