// tests/test_sync_applier.cpp — X1 phase 3: the ingestion-contract proof.
// prepareRoomSyncUpdate (worker-side, pure) produces the delta; TimelineState
// applies it (dedup, thread counts, group markers, cap-200 eviction).
#include "core/engine/sync_applier.hpp"
#include "core/engine/timeline_state.hpp"
#include "core/sync_engine.hpp"
#include "core/session_store.hpp"
#include "core/crypto/decryptor.hpp"
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

    // --- formatted_body variants + reply + m.file/m.audio (sync path) ---
    {
        auto testMsg = [](const std::string& content, const std::string& wantBody) {
            FastEvent fe;
            fe.type = "m.room.message";
            fe.eventId = "$f1";
            fe.senderId = "@alice:test";
            fe.contentJson = content;
            fe.originServerTs = 1;
            DisplayedEvent de;
            SyncApplier::fastEventToDisplayed(fe, de, "!r:test", nullptr);
            CHECK(de.body == wantBody, "applier: body extraction (" + content.substr(0, 30) + ")");
            return de;
        };
        // Plain formatted_body HTML string
        auto de1 = testMsg("{\"msgtype\":\"m.text\",\"formatted_body\":\"<b>bold</b> text\"}", "bold text");
        (void)de1;
        // Nested formatted_body object {"formatted_body":{"body":"..."}}
        auto de2 = testMsg("{\"msgtype\":\"m.text\",\"formatted_body\":{\"body\":\"<i>nested</i>\"}}", "nested");
        (void)de2;
        // Reply: fallback quote stripped + isReply/replyToEventId set
        auto de3 = testMsg("{\"msgtype\":\"m.text\",\"body\":\"> <@alice:test> original\\n\\nreply text\",\"m.relates_to\":{\"rel_type\":\"m.in_reply_to\",\"event_id\":\"$msg1\"}}", "reply text");
        CHECK(de3.isReply && de3.replyToEventId == "$msg1",
              "applier: sync-path reply extraction");
        // m.file: mxcUrl + filename body fallback
        auto de4 = testMsg("{\"msgtype\":\"m.file\",\"url\":\"mxc://server/file1\",\"filename\":\"report.pdf\"}", "report.pdf");
        CHECK(de4.mxcUrl == "mxc://server/file1", "applier: m.file mxcUrl parsed");
        // m.audio: mxcUrl parsed
        auto de5 = testMsg("{\"msgtype\":\"m.audio\",\"url\":\"mxc://server/audio1\"}", "");
        CHECK(de5.mxcUrl == "mxc://server/audio1", "applier: m.audio mxcUrl parsed");
    }

    // --- member-avatar extraction into currentRoomAvatars ---
    {
        FastEvent m;
        m.type = "m.room.member";
        m.eventId = "$mem1";
        m.senderId = "@alice:test";
        m.stateKey = "@alice:test";
        m.contentJson = "{\"membership\":\"join\",\"avatar_url\":\"mxc://server/ava1\"}";
        m.originServerTs = 1;
        FastRoom room;
        room.stateEvents = {m};
        FastSyncResponse resp;
        resp.joinedRooms.push_back({"!r2:test", std::move(room)});
        auto u = SyncApplier::prepareRoomSyncUpdate(resp, "!r2:test", "@me:test");
        auto it = u.currentRoomAvatars.find("@alice:test");
        CHECK(it != u.currentRoomAvatars.end() && it->second == "mxc://server/ava1",
              "applier: member avatar extracted into currentRoomAvatars");
    }

    // --- key-request retry backoff (pure decision) ---
    {
        CHECK(!progressive::desktop::shouldReRequestKey(0, 40000),
              "retry: attempt 0 (initial) never re-requests");
        CHECK(!progressive::desktop::shouldReRequestKey(1, 29000),
              "retry: first retry needs >=30s");
        CHECK(progressive::desktop::shouldReRequestKey(1, 31000),
              "retry: first retry after 30s");
        CHECK(!progressive::desktop::shouldReRequestKey(2, 119000),
              "retry: second retry needs >=2min");
        CHECK(progressive::desktop::shouldReRequestKey(2, 121000),
              "retry: second retry after 2min");
        CHECK(progressive::desktop::shouldReRequestKey(3, 601000),
              "retry: third retry after 10min");
        CHECK(progressive::desktop::shouldReRequestKey(4, 3601000),
              "retry: fourth retry after 1h");
        CHECK(!progressive::desktop::shouldReRequestKey(5, 3601000),
              "retry: capped after 4 retries");
    }

    // --- reactions never count as thread replies ---
    {
        DisplayedEvent root;
        root.eventId = "$root1";
        root.senderId = "@a:test";
        root.type = "m.room.message";
        root.msgtype = "m.text";
        root.body = "root";
        root.originServerTs = 1;
        TimelineState st;
        st.appendBack(root);
        DisplayedEvent react;
        react.eventId = "$react1";
        react.senderId = "@b:test";
        react.type = "m.reaction";
        react.isThreadReply = true;
        react.threadRootId = "$root1";
        react.originServerTs = 2;
        st.appendBack(react);
        CHECK(st.at(0)->threadReplyCount == 0, "applier: reactions never count as replies");
    }

    if (failures) { std::cerr << failures << " TEST(S) FAILED\n"; return 1; }
    std::cout << "All sync_applier tests passed\n";
    return 0;
}
