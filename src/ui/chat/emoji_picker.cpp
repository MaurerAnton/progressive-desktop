// src/ui/emoji_picker.cpp — emoji picker with search + font fallback.
#include "emoji_picker.hpp"

#include <QVBoxLayout>
#include <QLabel>
#include <QFont>
#include <QFontDatabase>
#include <QPushButton>
#include <QWidget>
#include <QPixmap>
#include <QPainter>
#include <algorithm>
#include <cstdio>

namespace progressive::desktop {

namespace {
inline constexpr int kPickerW      = 420;
inline constexpr int kPickerH      = 350;
inline constexpr int kSearchFont   = 11;
inline constexpr int kGridSpacing  = 2;
inline constexpr int kGridMargins  = 4;
inline constexpr int kEmojiCols    = 10;
inline constexpr int kEmojiFont    = 24;
inline constexpr int kEmojiBtnW    = 34;
inline constexpr int kEmojiBtnH    = 34;
inline constexpr int kIconW        = 28;
inline constexpr int kIconH        = 28;
inline constexpr int kRenderSz     = 32;
inline constexpr int kTabBarH      = 28;
} // namespace

namespace {

// Named emoji database. Each entry: (emoji, name, keywords).
// Names follow shortcodes similar to :short_code: used in Matrix.
const QVector<EmojiEntry> EMOJI_DB = {
    // Smileys
    {"😀", "grinning", "smile happy face"},
    {"😃", "smiley", "smile happy face eyes"},
    {"😄", "smile", "smile happy face eyes"},
    {"😁", "grin", "smile happy teeth"},
    {"😆", "laughing", "laugh happy squint"},
    {"😅", "sweat_smile", "sweat laugh happy"},
    {"😂", "joy", "laugh cry tears happy"},
    {"🤣", "rofl", "laugh rolling floor"},
    {"😊", "blush", "smile happy eyes shy"},
    {"😇", "innocent", "angel halo smile"},
    {"🙂", "slight_smile", "smile slight"},
    {"😉", "wink", "wink smile"},
    {"😌", "relieved", "relief calm"},
    {"😍", "heart_eyes", "love smile eyes heart"},
    {"🥰", "smiling_face_with_3_hearts", "love heart smile"},
    {"😘", "kissing_heart", "kiss love"},
    {"😋", "yum", "tongue food tasty"},
    {"😛", "stuck_out_tongue", "tongue"},
    {"😜", "stuck_out_tongue_winking_eye", "tongue wink"},
    {"🤪", "zany", "crazy wild"},
    {"😎", "sunglasses", "cool sun"},
    {"🤩", "star_struck", "star eyes"},
    {"🥳", "partying", "party celebrate"},
    {"😏", "smirk", "smirk smile"},
    {"😒", "unamused", "unhappy bored"},
    {"😞", "disappointed", "sad unhappy"},
    {"😔", "pensive", "sad thoughtful"},
    {"😕", "confused", "confuse"},
    {"🙁", "slight_frown", "frown"},
    {"☹️", "frowning2", "sad frown"},
    {"😢", "cry", "sad tear"},
    {"😭", "sob", "cry tears sad"},
    {"😤", "triumph", "angry steam"},
    {"😠", "angry", "mad"},
    {"😡", "rage", "red angry mad"},
    {"🤬", "cursing", "swear angry"},
    {"🤯", "exploding_head", "mind blown shock"},
    {"😳", "flushed", "blush red"},
    {"🥵", "hot", "heat red face"},
    {"🥶", "cold", "cold blue freeze"},
    {"😱", "scream", "fear shock"},
    {"😨", "fearful", "afraid scared"},
    {"😰", "cold_sweat", "sweat nervous"},
    {"🤗", "hugging", "hug"},
    {"🤔", "thinking", "think ponder"},
    {"🤭", "hand_over_mouth", "oops giggle"},
    {"🤫", "shushing", "quiet shh silent"},
    {"🤥", "lying", "lie pinocchio"},
    {"😶", "no_mouth", "mute silent"},
    {"😐", "neutral_face", "neutral"},
    {"😑", "expressionless", "blank"},
    {"😬", "grimacing", "teeth grimace"},
    {"🙄", "eye_roll", "roll eyes bored"},
    {"😴", "sleeping", "sleep zzz"},
    {"🤤", "drool", "sleep drool"},
    {"😷", "mask", "sick medical covid"},
    {"🤒", "thermometer", "sick thermometer"},
    {"🤕", "bandage", "hurt injured"},
    {"🤑", "money_mouth", "money rich"},
    // Hands
    {"👍", "thumbsup", "thumb up yes like"},
    {"👎", "thumbsdown", "thumb down no dislike"},
    {"👌", "ok_hand", "ok okay good"},
    {"✌️", "vulcan", "peace victory two"},
    {"🤞", "crossed_fingers", "luck hope"},
    {"🤟", "love_you", "love you gesture"},
    {"🤘", "metal", "rock horn"},
    {"🤙", "call_me", "phone hand"},
    {"👈", "point_left", "left point"},
    {"👉", "point_right", "right point"},
    {"👆", "point_up", "up point"},
    {"👇", "point_down", "down point"},
    {"☝️", "point_up_2", "one up point"},
    {"👋", "wave", "wave hello bye"},
    {"✋", "raised_hand", "hand stop"},
    {"👏", "clap", "clap applaud"},
    {"🙌", "raised_hands", "praise celebrate"},
    {"🤝", "handshake", "shake hands"},
    {"🙏", "pray", "pray please thanks"},
    {"💪", "muscle", "strong arm flex"},
    // Hearts
    {"❤️", "heart", "love red heart"},
    {"🧡", "orange_heart", "love orange"},
    {"💛", "yellow_heart", "love yellow"},
    {"💚", "green_heart", "love green"},
    {"💙", "blue_heart", "love blue"},
    {"💜", "purple_heart", "love purple"},
    {"🖤", "black_heart", "love black"},
    {"🤍", "white_heart", "love white"},
    {"💔", "broken_heart", "heart broken sad"},
    {"❣️", "heart_exclamation", "love exclaim"},
    {"💕", "two_hearts", "love"},
    {"💞", "revolving_hearts", "love"},
    {"💓", "heartbeat", "love heart beat"},
    {"💗", "heartpulse", "love growing"},
    {"💖", "sparkling_heart", "love sparkle"},
    {"💘", "cupid", "love arrow heart"},
    {"💝", "gift_heart", "love gift"},
    // Symbols
    {"❌", "x", "no cross wrong"},
    {"⭕", "o", "circle"},
    {"✅", "white_check_mark", "check yes ok"},
    {"❎", "negative_squared_cross_mark", "no"},
    {"🔥", "fire", "fire hot flame lit"},
    {"⭐", "star", "star favorite"},
    {"🌟", "star2", "glowing star"},
    {"✨", "sparkles", "sparkle shine"},
    {"⚡", "zap", "lightning fast"},
    {"💥", "boom", "explosion crash"},
    {"💫", "dizzy", "star dizzy"},
    // Party
    {"🎉", "tada", "party celebrate congrats"},
    {"🎊", "confetti_ball", "party confetti"},
    {"🎁", "gift", "present box"},
    {"🎂", "birthday", "cake birthday"},
    {"🍰", "cake", "dessert"},
    // Food
    {"🍔", "hamburger", "burger food"},
    {"🍟", "fries", "food potato"},
    {"🍕", "pizza", "food"},
    {"🌭", "hotdog", "food"},
    {"🍿", "popcorn", "food movie"},
    {"🥂", "champagne", "celebrate drink cheers"},
    {"🍻", "beers", "drink cheers"},
    {"🍷", "wine_glass", "drink"},
    {"🍸", "cocktail", "drink martini"},
    {"🍹", "tropical_drink", "drink"},
    {"🍺", "beer", "drink"},
    {"☕", "coffee", "drink hot"},
    {"🍵", "tea", "drink"},
    // Objects
    {"🕯️", "candle", "candle light fire wax"},
    {"💡", "bulb", "idea light"},
    {"📷", "camera", "photo picture"},
    {"📚", "books", "book read study"},
    {"📖", "book", "read"},
    {"✏️", "pencil2", "write edit"},
    {"📌", "pushpin", "pin"},
    {"📎", "paperclip", "attach file"},
    {"🔑", "key", "lock password"},
    {"🔒", "lock", "lock secure"},
    {"🔓", "unlock", "open"},
    // Animals
    {"🐶", "dog", "puppy pet"},
    {"🐱", "cat", "kitten pet"},
    {"🦊", "fox", "animal"},
    {"🐻", "bear", "animal"},
    {"🐼", "panda", "animal"},
    {"🦄", "unicorn", "magic animal"},
    {"🦋", "butterfly", "insect"},
    {"🐝", "bee", "insect honey"},
    {"🐢", "turtle", "animal slow"},
    {"🐙", "octopus", "animal sea"},
    {"🦉", "owl", "bird"},
    {"🦇", "bat", "animal"},
    {"🐺", "wolf", "animal"},
    {"🦕", "dinosaur", "dino"},
    // Weather
    {"☀️", "sunny", "sun weather hot"},
    {"🌙", "moon", "night"},
    {"☁️", "cloud", "weather"},
    {"🌧️", "rain", "weather rain"},
    {"⛈️", "thunder_cloud_rain", "storm"},
    {"❄️", "snowflake", "cold winter snow"},
    {"🌊", "ocean", "water sea wave"},
    {"🌈", "rainbow", "rainbow color"},
    // Activities
    {"⚽", "soccer", "ball sport football"},
    {"🏀", "basketball", "sport ball"},
    {"🎮", "video_game", "game console play"},
    {"🕹️", "joystick", "game play"},
    {"🎲", "dice", "game random"},
    {"🎵", "musical_note", "music"},
    {"🎶", "notes", "music"},
    {"🎸", "guitar", "music rock"},
    {"🎤", "microphone", "sing karaoke"},
    // Flags
    {"🚩", "flag", "flag red"},
    {"🏳️", "white_flag", "flag"},
    {"🏴", "black_flag", "flag"},
    // Misc
    {"💯", "100", "hundred perfect"},
    {"🆗", "ok", "okay"},
    {"🆒", "cool", "cool"},
    {"🆕", "new", "new"},
    {"🆙", "up", "up"},
    {"⁉️", "interrobang", "?!"},
    {"‼️", "bangbang", "!!"},
    {"❓", "question", "? ask"},
    {"❗", "exclamation", "!"},
    {"💤", "zzz", "sleep"},
    {"💬", "speech_balloon", "talk chat message"},
    {"💭", "thought_balloon", "think"},
    {"🕳️", "hole", "hole"},
    {"👀", "eyes", "look see watch"},
    // People
    {"🤷", "shrug", "shrug idk"},
    {"🤦", "facepalm", "face palm"},
    {"👶", "baby", "infant"},
    {"🧒", "child", "kid"},
    {"👦", "boy", "kid"},
    {"👧", "girl", "kid"},
    {"🧑", "adult", "person"},
    {"👨", "man", "person male"},
    {"👩", "woman", "person female"},
    {"👵", "older_woman", "grandma"},
    {"👴", "older_man", "grandpa"},
    {"👮", "cop", "police officer"},
    {"🕵️", "spy", "detective"},
    {"💂", "guard", "guard"},
    {"🧙", "mage", "wizard magic"},
    {"🧚", "fairy", "magic"},
    {"🧛", "vampire", "dracula"},
    {"🧟", "zombie", "undead"},
    {"🧞", "genie", "wish"},
    {"🧜", "merperson", "mermaid"},
};

// Find a font that supports emoji rendering on this system.
QString findEmojiFont() {
    static const QStringList candidates = {
        "Noto Color Emoji",
        "Apple Color Emoji",
        "Segoe UI Emoji",
        "Twemoji Mozilla",
        "OpenMoji Color",
        "OpenMoji",
        "EmojiOne",
    };
    QStringList families = QFontDatabase::families();
    for (const QString& name : candidates) {
        if (families.contains(name, Qt::CaseInsensitive)) {
            std::fprintf(stderr, "[emoji] using font: %s\n", name.toUtf8().data());
            return name;
        }
    }
    std::fprintf(stderr, "[emoji] WARNING: no emoji font found — emoji will render as grey squares\n");
    return {};
}

} // namespace

EmojiPicker::EmojiPicker(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Pick emoji");
    setModal(true);
    resize(kPickerW, kPickerH);

    searchEdit_ = new QLineEdit(this);
    searchEdit_->setPlaceholderText("Search emoji (e.g. candle, fire, heart)...");
    searchEdit_->setClearButtonEnabled(true);

    // Set a slightly larger font for search
    QFont searchFont = searchEdit_->font();
    searchFont.setPointSize(kSearchFont);
    searchEdit_->setFont(searchFont);

    grid_ = new QGridLayout;
    grid_->setSpacing(kGridSpacing);
    grid_->setContentsMargins(kGridMargins, kGridMargins, kGridMargins, kGridMargins);

    auto* scrollContent = new QWidget;
    scrollContent->setLayout(grid_);

    scrollArea_ = new QScrollArea(this);
    scrollArea_->setWidget(scrollContent);
    scrollArea_->setWidgetResizable(true);
    scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* root = new QVBoxLayout(this);
    root->addWidget(searchEdit_);
    root->addWidget(scrollArea_);

    populateEmojis();
    buildGrid();

    connect(searchEdit_, &QLineEdit::textChanged, this, &EmojiPicker::onSearchChanged);
}

void EmojiPicker::populateEmojis() {
    allEmojis_ = EMOJI_DB;
}

void EmojiPicker::buildGrid() {
    // Clear existing widgets from grid
    QLayoutItem* item;
    while ((item = grid_->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }

    // Find an emoji-capable font for rendering the buttons
    static QString emojiFont = findEmojiFont();

    int cols = kEmojiCols;
    buttons_.clear();
    buttons_.reserve(allEmojis_.size());

    // Build a hidden QLabel to render emoji text into QPixmaps.
    // QLabel supports color glyphs; QPushButton text does not on Linux/Qt6.
    // We render each emoji to a 28x28 pixmap and use it as QIcon.
    QLabel renderLabel;
    QFont emojiF = renderLabel.font();
    if (!emojiFont.isEmpty()) emojiF.setFamily(emojiFont);
    emojiF.setPixelSize(kEmojiFont);
    renderLabel.setFont(emojiF);
    renderLabel.setAlignment(Qt::AlignCenter);
    renderLabel.setFixedSize(kRenderSz, kRenderSz);

    for (int i = 0; i < allEmojis_.size(); ++i) {
        const auto& entry = allEmojis_[i];

        // Render emoji to pixmap via QLabel
        renderLabel.setText(entry.emoji);
        QPixmap pix(kRenderSz, kRenderSz);
        pix.fill(Qt::transparent);
        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing);
        renderLabel.render(&p, QPoint(0, 0), QRegion(0, 0, kRenderSz, kRenderSz));
        p.end();

        auto* btn = new QPushButton(this);
        btn->setFixedSize(kEmojiBtnW, kEmojiBtnH);
        btn->setIcon(QIcon(pix));
        btn->setIconSize(QSize(kIconW, kIconH));
        btn->setToolTip(entry.emoji + " " + entry.name + " — " + entry.keywords);
        btn->setFlat(true);
        btn->setStyleSheet("QPushButton { border: none; background: transparent; }"
                           "QPushButton:hover { background: #3a3a3a; border-radius: 4px; }");
        grid_->addWidget(btn, i / cols, i % cols);
        connect(btn, &QPushButton::clicked, this, [this, emoji = entry.emoji]() {
            onEmojiClicked(emoji);
        });
        buttons_.push_back(btn);
    }
}

void EmojiPicker::onSearchChanged(const QString& text) {
    // Show/hide buttons based on search — much faster than rebuilding.
    if (text.isEmpty()) {
        for (auto* btn : buttons_) btn->show();
    } else {
        QString needle = text.toLower().trimmed();
        for (int i = 0; i < buttons_.size() && i < allEmojis_.size(); ++i) {
            const auto& e = allEmojis_[i];
            if (e.name.contains(needle, Qt::CaseInsensitive) ||
                e.keywords.contains(needle, Qt::CaseInsensitive)) {
                buttons_[i]->show();
            } else {
                buttons_[i]->hide();
            }
        }
    }
}

void EmojiPicker::onEmojiClicked(const QString& emoji) {
    emit emojiSelected(emoji);
    accept();
}

} // namespace progressive::desktop
