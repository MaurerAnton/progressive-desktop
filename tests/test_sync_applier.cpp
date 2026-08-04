// tests/test_sync_applier.cpp — X1 phase 3: the ingestion-contract proof.
// prepareRoomSyncUpdate (worker-side, pure) produces the delta; TimelineState
// applies it (dedup, thread counts, group markers, cap-200 eviction).
#include "core/engine/sync_applier.hpp"
#include "core/engine/timeline_state.hpp"
#include "core/sync_engine.hpp"
#include "core/session_store.hpp"
#include "core/matrix_client.hpp"

#include <iostream>
#include <string>
#include <deque>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::cerr << "FAIL: " << msg << " (line " << __LINE__ << ")\n"; failures++; } \
    else { std::cout << "ok: " << msg << "\n"; } \
} while (0)

using namespace progressive::desktop;

static void test_initialize_e2ee() {
    using namespace progressive::desktop;
    auto client = std::make_shared<MatrixClient>();
    AccountInfo acct;
    acct.userId = "@u:test";
    acct.deviceId = "DEV";
    acct.homeserverUrl = "http://127.0.0.1:1";  // no server — the enqueued upload just fails fast
    acct.accessToken = "t";
    client->setAccount(acct);

    auto store = std::make_shared<SessionStore>();
    CHECK(store->open("/tmp/pd_x1_e2ee.db"), "e2ee: store open");

    SyncEngine se;
    se.setClient(client);
    se.setSessionStore(store);
    auto r = se.initializeE2EE();
    CHECK(r.e2eeOk, "e2ee: account initialized");
    CHECK(se.decryptor()->isInitialized(), "e2ee: decryptor initialized");
    CHECK(store->loadOlmAccount("@u:test/DEV").has_value(), "e2ee: account pickle saved");

    // Reload from the saved pickle.
    SyncEngine se2;
    se2.setClient(client);
    se2.setSessionStore(store);
    auto r2 = se2.initializeE2EE();
    CHECK(r2.e2eeOk, "e2ee: reload from pickle");
    CHECK(se2.decryptor()->isInitialized(), "e2ee: reloaded decryptor initialized");
}

int main() {
    test_initialize_e2ee();
    auto owned = std::make_shared<std::deque<std::string>>();
    owned->push_back("!room1:test");   // 0 room id
    owned->push_back("$msg1");         // 1
    owned->push_back("@alice:test");   // 2
    owned->push_back("m.room.message");// 3
    owned->push_back("{\"msgtype\":\"m.text\",\"body\":\"hello\"}");               // 4
    owned->push_back("$thread1");      // 5
    owned->push_back("$reply1");       // 6
    owned->push_back("{\"msgtype\":\"m.text\",\"body\":\"reply\",\"m.relates_to\":{\"rel_type\":\"m.thread\",\"event_id\":\"$msg1\"}}");  // 7

    FastSyncResponse resp;
    resp.ownedContentStrings = owned;
    FastRoom room;
    FastEvent e1;
    e1.type = (*owned)[3]; e1.eventId = (*owned)[1]; e1.senderId = (*owned)[2];
    e1.contentJson = (*owned)[4]; e1.originServerTs = 1000;
    FastEvent e2;
    e2.type = (*owned)[3]; e2.eventId = (*owned)[6]; e2.senderId = (*owned)[2];
    e2.contentJson = (*owned)[7]; e2.originServerTs = 2000;
    room.timeline.events = {e1, e2};
    resp.joinedRooms.push_back({(*owned)[0], std::move(room)});
    auto u = SyncApplier::prepareRoomSyncUpdate(resp, "!room1:test", "@me:test");
    CHECK(u.roomsToUpsert.size() == 1, "applier: one room upserted");
    CHECK(u.currentRoomUpdated && u.currentRoomEvents.size() == 2,
          "applier: current room events captured");
    CHECK(u.inviteCount == 0, "applier: no invites");

    std::vector<DisplayedEvent> events;
    int convIdx = 0;
    for (const auto& fe : u.currentRoomEvents) {
        DisplayedEvent de;
        SyncApplier::fastEventToDisplayed(fe, de, u.currentRoomId, nullptr);
        events.push_back(std::move(de));
        convIdx++;
    }
    TimelineState st;
    auto r1 = st.appendBackBatch(events);
    CHECK(r1.changed && r1.firstRow == 0 && r1.lastRow == 1, "applier: batch appended");
    CHECK(st.size() == 2, "applier: two events");
    CHECK(st.at(1)->isThreadReply && st.at(1)->threadRootId == "$msg1",
          "applier: thread reply parsed");
    CHECK(st.at(0)->threadReplyCount == 1, "applier: thread root count incremented");
    CHECK(!st.at(1)->groupFirst, "applier: same-sender within window merges");

    auto r2 = st.appendBackBatch(events);
    CHECK(!r2.changed, "applier: dedup prevents duplicates");
    CHECK(st.size() == 2, "applier: size unchanged after dedup");

    TimelineState big;
    for (int i = 0; i < 250; ++i) {
        DisplayedEvent de;
        de.eventId = "$cap" + std::to_string(i);
        de.senderId = "@a:test";
        de.type = "m.room.message";
        de.msgtype = "m.text";
        de.body = "x";
        de.originServerTs = i;
        big.appendBack(de);
    }
    CHECK(big.size() <= static_cast<size_t>(TimelineState::MAX_TIMELINE_EVENTS),
          "applier: cap-200 enforced");
    CHECK(big.at(0)->eventId == "$cap50", "applier: oldest evicted");

    if (failures) { std::cerr << failures << " TEST(S) FAILED\n"; return 1; }
    std::cout << "All sync_applier tests passed\n";
    return 0;
}
