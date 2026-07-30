// src/ui/room/event_parser.hpp — shared simdjson event field extraction.
#pragma once
#include <string>
#include <simdjson.h>

namespace progressive::desktop {

struct DisplayedEvent;

// Extracts common fields from a simdjson event object into `out`:
//   type, event_id, sender, origin_server_ts, state_key, contentJson,
//   senderName (derived from senderId @user:server -> user).
// Returns true if type was found.
bool parseEventFields(simdjson::dom::element evt, DisplayedEvent& out);

// Returns a date divider label for a timestamp ("Today", "Yesterday", "Mon 12 Aug").
std::string dateDividerLabel(int64_t originServerTs);

} // namespace progressive::desktop
