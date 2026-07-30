// src/core/crypto/sas_emojis.cpp
#include "sas_emojis.hpp"
#include <sstream>
#include <iomanip>

namespace progressive::desktop {

const std::vector<VerificationEmoji>& sasEmojiTable() {
    static const std::vector<VerificationEmoji> table = {
        {"🐶", "Dog"}, {"🐱", "Cat"}, {"🦁", "Lion"}, {"🐎", "Horse"},
        {"🦄", "Unicorn"}, {"🐷", "Pig"}, {"🐘", "Elephant"}, {"🐰", "Rabbit"},
        {"🐼", "Panda"}, {"🐓", "Rooster"}, {"🐧", "Penguin"}, {"🐢", "Turtle"},
        {"🐙", "Octopus"}, {"🐳", "Whale"}, {"🦋", "Butterfly"}, {"🌻", "Sunflower"},
        {"🌴", "Palm Tree"}, {"🌵", "Cactus"}, {"🍇", "Grapes"}, {"🍉", "Watermelon"},
        {"🍋", "Lemon"}, {"🍌", "Banana"}, {"🍍", "Pineapple"}, {"🍎", "Red Apple"},
        {"🍒", "Cherries"}, {"🍓", "Strawberry"}, {"🌽", "Corn"}, {"🍕", "Pizza"},
        {"🎂", "Birthday Cake"}, {"🏆", "Trophy"}, {"🎓", "Graduation Cap"},
        {"🎸", "Guitar"}, {"🎺", "Trumpet"}, {"🔔", "Bell"}, {"🎵", "Musical Note"},
        {"🎄", "Christmas Tree"}, {"🎃", "Pumpkin"}, {"🌎", "Earth"}, {"🌙", "Moon"},
        {"☀️", "Sun"}, {"⭐", "Star"}, {"⚡", "Lightning"}, {"🔥", "Fire"},
        {"🌈", "Rainbow"}, {"❄️", "Snowflake"}, {"💧", "Droplet"}, {"🎈", "Balloon"},
        {"🔑", "Key"}, {"🔒", "Lock"}, {"✏️", "Pencil"}, {"📌", "Pin"},
        {"⌚", "Watch"}, {"📷", "Camera"}, {"🔋", "Battery"}, {"💡", "Light Bulb"},
        {"🏁", "Checkered Flag"}, {"🚀", "Rocket"}, {"🚲", "Bicycle"}, {"🚗", "Car"},
        {"⛵", "Sailboat"}, {"✈️", "Airplane"}, {"🚂", "Train"}, {"🚦", "Traffic Light"}
    };
    return table;
}

std::vector<VerificationEmoji> computeSasEmojis(const std::string& sasBytes) {
    std::vector<VerificationEmoji> result;
    auto& allEmojis = sasEmojiTable();
    for (size_t i = 0; i < sasBytes.size() && result.size() < 7; ++i) {
        unsigned char byte = sasBytes[i];
        int idx = byte & 0x3F;
        if (idx < (int)allEmojis.size()) {
            result.push_back(allEmojis[idx]);
        }
    }
    return result;
}

std::vector<int> computeSasDecimals(const std::string& sasBytes) {
    std::vector<int> decimals(7);
    if (sasBytes.size() < 6) return decimals;

    uint64_t num = 0;
    for (int i = 0; i < 6; i++) {
        num = (num << 8) | static_cast<uint8_t>(sasBytes[i]);
    }
    for (int i = 0; i < 7; i++) {
        int shift = 48 - (i + 1) * 13;
        if (shift < 0) shift = 0;
        decimals[i] = static_cast<int>(((num >> shift) & 0x1FFF) % 10000);
    }
    return decimals;
}

std::string formatSasEmojis(const std::vector<VerificationEmoji>& emojis) {
    std::ostringstream out;
    for (size_t i = 0; i < emojis.size(); ++i) {
        if (i > 0) out << "  ";
        out << emojis[i].emoji;
    }
    return out.str();
}

std::string formatSasDecimals(const std::vector<int>& decimals) {
    std::ostringstream out;
    for (size_t i = 0; i < decimals.size(); ++i) {
        if (i > 0) out << " - ";
        out << std::setfill('0') << std::setw(3) << (decimals[i] % 1000);
    }
    return out.str();
}

} // namespace progressive::desktop
