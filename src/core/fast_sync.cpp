// src/core/fast_sync.cpp — simdjson-based /sync parser implementation.

#include "fast_sync.hpp"

#include <simdjson.h>

#include <stdexcept>
#include <sstream>
#include <utility>

namespace progressive::desktop {

namespace {

using dom_value = simdjson::simdjson_result<simdjson::dom::element>;

// Helper: get a string_view from a simdjson element. Returns empty on missing/error.
// Uses get_string() (returns std::string_view — same type, just different name).
std::string_view getStringOrEmpty(simdjson::dom::element parent, std::string_view key) {
    auto r = parent[key].get_string();
    if (r.error() == simdjson::SUCCESS) return r.value();
    return {};
}

// Get an int64 from a simdjson element. Returns 0 on missing/error.
int64_t getIntOrZero(simdjson::dom::element parent, std::string_view key) {
    auto r = parent[key].get_int64();
    if (r.error() == simdjson::SUCCESS) return r.value();
    return 0;
}

bool getBoolOrFalse(simdjson::dom::element parent, std::string_view key) {
    auto r = parent[key].get_bool();
    if (r.error() == simdjson::SUCCESS) return r.value();
    return false;
}

// Accept a simdjson_result<element> (e.g. from evt["type"]) and extract
// string_view. Returns empty on ANY error (missing key, wrong type, etc.).
// This replaces the old unwrap() + sv() pattern that crashed on missing keys
// because unwrap() returned a default-constructed element with no usable tape.
inline std::string_view sv(simdjson::simdjson_result<simdjson::dom::element> r) {
    if (r.error() != simdjson::SUCCESS) return {};
    auto sr = r.value().get_string();
    return sr.error() == simdjson::SUCCESS ? std::string_view(sr.value()) : std::string_view{};
}

// Build a FastEvent from a simdjson event object.ownedStrings — the deque to
// store serialized contentJson (string_view points into it).
FastEvent buildFastEvent(simdjson::dom::element evt,
                          std::deque<std::string>& ownedStrings) {
    FastEvent e;
    e.type            = sv(evt["type"]);
    e.eventId         = sv(evt["event_id"]);
    e.senderId        = sv(evt["sender"]);
    e.stateKey        = sv(evt["state_key"]);
    e.originServerTs = getIntOrZero(evt, "origin_server_ts");

    // contentJson — serialize the "content" object back to JSON so
    // progressive_native's parsers (which take const std::string&) can
    // extract fields from it. We store the string in ownedStrings (deque,
    // stable back-inserts) and store a view into it.
    auto contentResult = evt["content"];
    if (contentResult.error() == simdjson::SUCCESS) {
        auto content = contentResult.value();
        ownedStrings.push_back(simdjson::to_string(content));
        e.contentJson = ownedStrings.back();
    }
    return e;
}

// Parse one joined room: { "state": {...}, "timeline": {...}, "unread_notifications": {...} }
FastRoom buildFastRoom(simdjson::dom::element room,
                        std::deque<std::string>& ownedStrings) {
    FastRoom r;

    // State events
    auto stateResult = room["state"];
    if (stateResult.error() == simdjson::SUCCESS) {
        auto state = stateResult.value();
        auto stateEvents = state["events"].get_array();
        if (stateEvents.error() == simdjson::SUCCESS) {
            for (auto evt : stateEvents.value()) {
                FastEvent fe = buildFastEvent(evt, ownedStrings);
                if (fe.type == "m.room.encryption") r.isEncrypted = true;
                r.stateEvents.push_back(std::move(fe));
            }
        }
    }

    // Timeline events
    auto timelineResult = room["timeline"];
    if (timelineResult.error() == simdjson::SUCCESS) {
        auto timeline = timelineResult.value();
        r.timeline.limited = getBoolOrFalse(timeline, "limited");
        r.timeline.prevToken = getStringOrEmpty(timeline, "prev_batch");
        auto tlEvents = timeline["events"].get_array();
        if (tlEvents.error() == simdjson::SUCCESS) {
            for (auto evt : tlEvents.value()) {
                r.timeline.events.push_back(buildFastEvent(evt, ownedStrings));
            }
        }
    }

    // Unread notifications
    auto unreadResult = room["unread_notifications"];
    if (unreadResult.error() == simdjson::SUCCESS) {
        auto unread = unreadResult.value();
        r.notificationCount = static_cast<int>(getIntOrZero(unread, "notification_count"));
        r.highlightCount   = static_cast<int>(getIntOrZero(unread, "highlight_count"));
    }

    return r;
}

} // namespace

std::string_view FastRoom::name() const {
    for (const auto& e : stateEvents) {
        if (e.type == "m.room.name" && !e.contentJson.empty()) {
            // Inline parse — look for "name":"..." in the content JSON.
            // The content is small (~50 bytes) so even a string scan is fast.
            // Using simdjson here would require a parser — too heavy for inline.
            std::string_view key = "\"name\":\"";
            auto pos = e.contentJson.find(key);
            if (pos != std::string_view::npos) {
                pos += key.size();
                auto end = e.contentJson.find('"', pos);
                if (end != std::string_view::npos) {
                    return e.contentJson.substr(pos, end - pos);
                }
            }
            // Try with space: "name": "..."
            key = "\"name\": \"";
            pos = e.contentJson.find(key);
            if (pos != std::string_view::npos) {
                pos += key.size();
                auto end = e.contentJson.find('"', pos);
                if (end != std::string_view::npos) {
                    return e.contentJson.substr(pos, end - pos);
                }
            }
        }
    }
    return {};
}

FastSyncResponse parseSyncResponseFast(std::string json, std::string& errorMessage) {
    FastSyncResponse resp;
    errorMessage.clear();

    auto parser = std::make_shared<simdjson::dom::parser>();
    auto ownedStrings = std::make_shared<std::deque<std::string>>();
    simdjson::dom::element root;

    auto result = parser->parse(json);
    if (result.error() != simdjson::SUCCESS) {
        errorMessage = std::string("simdjson parse error: ") + simdjson::error_message(result.error());
        resp.buffer = std::make_shared<std::string>(std::move(json));
        resp.parser = parser;
        resp.ownedContentStrings = ownedStrings;
        return resp;
    }
    root = result.value();

    // next_batch
    resp.nextBatch = getStringOrEmpty(root, "next_batch");

    // rooms.join
    auto roomsResult = root["rooms"];
    if (roomsResult.error() == simdjson::SUCCESS) {
        auto rooms = roomsResult.value();
        auto joinResult = rooms["join"].get_object();
        if (joinResult.error() == simdjson::SUCCESS) {
            auto join = joinResult.value();
            for (auto field : join) {
                std::string_view roomId(field.key);
                FastRoom room = buildFastRoom(field.value, *ownedStrings);
                resp.totalTimelineEvents += static_cast<int>(room.timeline.events.size());
                resp.joinedRooms.emplace_back(roomId, std::move(room));
            }
        }

        auto inviteResult = rooms["invite"].get_object();
        if (inviteResult.error() == simdjson::SUCCESS) {
            for (auto field : inviteResult.value()) {
                resp.invitedRoomIds.emplace_back(field.key);
            }
        }

        auto leaveResult = rooms["leave"].get_object();
        if (leaveResult.error() == simdjson::SUCCESS) {
            for (auto field : leaveResult.value()) {
                resp.leftRoomIds.emplace_back(field.key);
            }
        }
    }

    auto toDeviceResult = root["to_device"];
    if (toDeviceResult.error() == simdjson::SUCCESS) {
        auto toDevice = toDeviceResult.value();
        auto eventsResult = toDevice["events"].get_array();
        if (eventsResult.error() == simdjson::SUCCESS) {
            for (auto evt : eventsResult.value()) {
                // Build a FastEvent for each to-device event. We serialize
                // the content into ownedContentStrings so string_views persist.
                FastEvent fe = buildFastEvent(evt, *ownedStrings);
                resp.toDeviceEventList.push_back(std::move(fe));
            }
            resp.toDeviceEvents = static_cast<int>(resp.toDeviceEventList.size());
        }
    }

    resp.buffer = std::make_shared<std::string>(std::move(json));
    resp.parser = parser;
    resp.ownedContentStrings = ownedStrings;

    return resp;
}

} // namespace progressive::desktop
