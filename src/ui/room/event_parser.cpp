// src/ui/room/event_parser.cpp
#include "event_parser.hpp"
#include "../timeline/timeline_model.hpp"

namespace progressive::desktop {

static std::string sv(simdjson::dom::element e) {
    auto r = e.get_string();
    return r.error() == simdjson::SUCCESS ? std::string(r.value()) : std::string();
}

bool parseEventFields(simdjson::dom::element evt, DisplayedEvent& out) {
    out.type = sv(evt["type"]);
    out.eventId = sv(evt["event_id"]);
    out.senderId = sv(evt["sender"]);
    auto ts = evt["origin_server_ts"].get_int64();
    if (ts.error() == simdjson::SUCCESS) out.originServerTs = ts.value();
    auto cr = evt["content"];
    if (cr.error() == simdjson::SUCCESS)
        out.contentJson = simdjson::to_string(cr.value());
    if (!out.senderId.empty() && out.senderId[0] == '@') {
        auto c = out.senderId.find(':');
        out.senderName = (c != std::string::npos) ? out.senderId.substr(1, c - 1)
                                                  : out.senderId.substr(1);
    }
    return !out.type.empty();
}

} // namespace progressive::desktop
