// src/ui/room/event_body_parser.hpp — shared plaintext JSON parsing helper.
//
// Used by both fastEventToDisplayed (sync path) and loadHistory (history path)
// to parse decrypted Megolm plaintext into event type and content JSON.
#pragma once

#include <string>
#include <simdjson.h>

namespace progressive::desktop {

// Parse a decrypted Megolm plaintext JSON into `outType` and `outContentJson`.
// The plaintext JSON format is:
//   {"type":"m.room.message","content":{"body":"...","msgtype":"...","url":"..."}}
// Returns true if parsing succeeded, false otherwise.
inline bool parsePlaintextBody(const std::string& plaintextJson,
                               std::string& outType,
                               std::string& outContentJson) {
    simdjson::dom::parser parser;
    auto root = parser.parse(plaintextJson);
    if (root.error() != simdjson::SUCCESS) return false;
    auto t = root.value()["type"].get_string();
    if (t.error() != simdjson::SUCCESS) return false;
    outType = std::string(t.value());
    auto cr = root.value()["content"];
    if (cr.error() == simdjson::SUCCESS) {
        outContentJson = simdjson::to_string(cr.value());
    }
    return true;
}

} // namespace progressive::desktop
