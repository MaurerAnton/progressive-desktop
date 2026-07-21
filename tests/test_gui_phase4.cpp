// tests/test_gui_phase4.cpp — GUI test for RoomListModel lazy state loading.
//
// Verifies:
//  1. RoomListModel manages rooms with upsert + find
//  2. Room initially appears with roomId as name, then name is updated
//  3. data() returns correct values for all roles
//  4. Model handles batch state updates correctly
//
// Build + run:
//   cmake --build build -j4 && ./build/test_gui_phase4

#include <QApplication>
#include <QAbstractListModel>
#include <QModelIndex>
#include <QString>
#include <iostream>
#include <cstdio>

// Minimal RoomListModel (same as production but standalone for testing)
// We replicate the essential parts to test lazily-loaded state.

struct RoomData {
    std::string roomId;
    std::string name;
    std::string lastMessage;
    int64_t lastActivityTs = 0;
    bool isInvite = false;
    std::string avatarUrl;
};

class RoomListModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        NameRole = Qt::DisplayRole,
        RoomIdRole = Qt::UserRole + 1,
        AvatarUrlRole = Qt::UserRole + 2,
        IsInviteRole = Qt::UserRole + 3,
    };

    explicit RoomListModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex& parent = QModelIndex()) const override {
        (void)parent;
        return static_cast<int>(rooms_.size());
    }

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override {
        if (!index.isValid() || index.row() >= static_cast<int>(rooms_.size()))
            return {};
        const auto& r = rooms_[index.row()];
        switch (role) {
            case NameRole:     return QString::fromStdString(r.name);
            case RoomIdRole:   return QString::fromStdString(r.roomId);
            case AvatarUrlRole: return QString::fromStdString(r.avatarUrl);
            case IsInviteRole: return r.isInvite;
            default: return {};
        }
    }

    bool upsertRoom(const RoomData& room) {
        auto it = index_.find(room.roomId);
        if (it != index_.end()) {
            int row = it->second;
            rooms_[row] = room;
            emit dataChanged(index(row), index(row));
            return false;  // updated, not inserted
        }
        int pos = static_cast<int>(rooms_.size());
        beginInsertRows(QModelIndex(), pos, pos);
        rooms_.push_back(room);
        index_[room.roomId] = pos;
        endInsertRows();
        return true;  // inserted
    }

    bool removeRoom(const std::string& roomId) {
        auto it = index_.find(roomId);
        if (it == index_.end()) return false;
        int row = it->second;
        beginRemoveRows(QModelIndex(), row, row);
        rooms_.erase(rooms_.begin() + row);
        endRemoveRows();
        rebuildIndex();
        return true;
    }

    const RoomData* at(int row) const {
        if (row < 0 || row >= static_cast<int>(rooms_.size())) return nullptr;
        return &rooms_[row];
    }

    int findRowByRoomId(const std::string& roomId) const {
        auto it = index_.find(roomId);
        return it != index_.end() ? it->second : -1;
    }

    void clear() {
        beginResetModel();
        rooms_.clear();
        index_.clear();
        endResetModel();
    }

private:
    void rebuildIndex() {
        index_.clear();
        for (int i = 0; i < static_cast<int>(rooms_.size()); ++i)
            index_[rooms_[i].roomId] = i;
    }

    std::vector<RoomData> rooms_;
    std::unordered_map<std::string, int> index_;
};

// ---- Test infrastructure ----
static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::cerr << "FAIL: " << msg << " (line " << __LINE__ << ")\n"; failures++; } \
    else { std::cout << "ok: " << msg << "\n"; } \
} while (0)
#define CHECK_EQ(a, b, msg) do { \
    if ((a) != (b)) { std::cerr << "FAIL: " << msg << " (expected " << (b) << " got " << (a) << ") line " << __LINE__ << "\n"; failures++; } \
    else { std::cout << "ok: " << msg << "\n"; } \
} while (0)

// ---- Tests ----

static void test_empty_model() {
    RoomListModel m;
    CHECK_EQ(m.rowCount(QModelIndex()), 0, "empty model has 0 rows");
}

static void test_upsert_insert() {
    RoomListModel m;
    RoomData r1{"!a:example.org", "Room A", "", 0, false, ""};
    bool inserted = m.upsertRoom(r1);
    CHECK(inserted, "upsert returns true for insert");
    CHECK_EQ(m.rowCount(QModelIndex()), 1, "model has 1 row after insert");

    QModelIndex idx = m.index(0);
    CHECK_EQ(idx.data(RoomListModel::NameRole).toString().toStdString(), "Room A",
        "data returns correct name");
    CHECK_EQ(idx.data(RoomListModel::RoomIdRole).toString().toStdString(), "!a:example.org",
        "data returns correct roomId");
}

static void test_upsert_update() {
    RoomListModel m;
    RoomData r1{"!a:example.org", "!a:example.org", "", 0, false, ""};
    m.upsertRoom(r1);
    CHECK_EQ(m.rowCount(QModelIndex()), 1, "model has 1 row");

    RoomData r2{"!a:example.org", "Updated Name", "", 0, false, "mxc://m.org/av"};
    bool inserted = m.upsertRoom(r2);
    CHECK(!inserted, "upsert returns false for update");
    CHECK_EQ(m.rowCount(QModelIndex()), 1, "model still has 1 row after update");

    QModelIndex idx = m.index(0);
    CHECK_EQ(idx.data(RoomListModel::NameRole).toString().toStdString(), "Updated Name",
        "data returns updated name");
    CHECK_EQ(idx.data(RoomListModel::AvatarUrlRole).toString().toStdString(), "mxc://m.org/av",
        "data returns updated avatar");
}

static void test_find_room() {
    RoomListModel m;
    RoomData r1{"!a:example.org", "A", "", 0, false, ""};
    RoomData r2{"!b:example.org", "B", "", 0, false, ""};
    m.upsertRoom(r1);
    m.upsertRoom(r2);

    CHECK_EQ(m.findRowByRoomId("!a:example.org"), 0, "findRoom 'a' at row 0");
    CHECK_EQ(m.findRowByRoomId("!b:example.org"), 1, "findRoom 'b' at row 1");
    CHECK_EQ(m.findRowByRoomId("!c:example.org"), -1, "findRoom missing returns -1");
}

static void test_remove_room() {
    RoomListModel m;
    m.upsertRoom({"!a:example.org", "A", "", 0, false, ""});
    m.upsertRoom({"!b:example.org", "B", "", 0, false, ""});
    CHECK_EQ(m.rowCount(QModelIndex()), 2, "2 rooms before remove");

    bool removed = m.removeRoom("!a:example.org");
    CHECK(removed, "removeRoom returns true");
    CHECK_EQ(m.rowCount(QModelIndex()), 1, "1 room after remove");
    CHECK_EQ(m.findRowByRoomId("!b:example.org"), 0, "remaining room at row 0");
}

static void test_lazy_state_scenario() {
    // Simulate: room arrives from sync with roomId as name (no state events).
    // Then a lazy fetch returns the real name and avatar. Model should update.

    RoomListModel m;

    // Step 1: initial sync — no state for this room, name == roomId
    RoomData r{"!dev:matrix.org", "!dev:matrix.org", "Hello", 1000, false, ""};
    m.upsertRoom(r);

    QModelIndex idx = m.index(0);
    CHECK_EQ(idx.data(RoomListModel::NameRole).toString().toStdString(), "!dev:matrix.org",
        "initial name is roomId (no state)");

    // Step 2: lazy fetch returns m.room.name → update the model
    auto* rd = const_cast<RoomData*>(m.at(0));
    rd->name = "Development Team";
    emit m.dataChanged(m.index(0), m.index(0));

    CHECK_EQ(m.index(0).data(RoomListModel::NameRole).toString().toStdString(),
        "Development Team", "name updated after lazy fetch");
}

static void test_multiple_rooms_lazy_mixed() {
    RoomListModel m;

    // Sync gives 3 rooms
    m.upsertRoom({"!a:example.org", "Team Alpha", "msg1", 10, false, "mxc://a"});
    m.upsertRoom({"!b:example.org", "!b:example.org", "msg2", 20, false, ""});  // needs state
    m.upsertRoom({"!c:example.org", "Dev Chat", "msg3", 30, false, ""});        // has name, no avatar

    CHECK_EQ(m.rowCount(QModelIndex()), 3, "3 rooms after sync");

    // Which rooms need lazy fetch?
    int needFetch = 0;
    for (int i = 0; i < m.rowCount(QModelIndex()); ++i) {
        auto* rd = m.at(i);
        if (rd->name == rd->roomId || rd->avatarUrl.empty()) needFetch++;
    }
    CHECK_EQ(needFetch, 2, "2 rooms need lazy fetch (b needs name+avatar, c needs avatar)");

    // Simulate lazy fetch for room b
    auto* b = const_cast<RoomData*>(m.at(1));
    b->name = "Beta Room";
    b->avatarUrl = "mxc://b";
    emit m.dataChanged(m.index(1), m.index(1));

    CHECK_EQ(m.index(1).data(RoomListModel::NameRole).toString().toStdString(),
        "Beta Room", "room b name updated");

    // Simulate lazy fetch for room c
    auto* c = const_cast<RoomData*>(m.at(2));
    c->avatarUrl = "mxc://c";
    emit m.dataChanged(m.index(2), m.index(2));

    CHECK_EQ(m.index(2).data(RoomListModel::AvatarUrlRole).toString().toStdString(),
        "mxc://c", "room c avatar updated");
}

static void test_clear_model() {
    RoomListModel m;
    m.upsertRoom({"!a:example.org", "A", "", 0, false, ""});
    m.upsertRoom({"!b:example.org", "B", "", 0, false, ""});
    m.clear();
    CHECK_EQ(m.rowCount(QModelIndex()), 0, "clear empties model");
    CHECK_EQ(m.findRowByRoomId("!a:example.org"), -1, "find returns -1 after clear");
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    std::cout << "=== Phase 4: GUI RoomListModel Lazy State Tests ===\n\n";

    test_empty_model();
    test_upsert_insert();
    test_upsert_update();
    test_find_room();
    test_remove_room();
    test_lazy_state_scenario();
    test_multiple_rooms_lazy_mixed();
    test_clear_model();

    std::cout << "\n";

    if (failures == 0) {
        std::cout << "=== ALL GUI TESTS PASSED ===" << std::endl;
        return 0;
    } else {
        std::cerr << "=== " << failures << " TEST(S) FAILED ===" << std::endl;
        return 1;
    }
}

#include "test_gui_phase4.moc"
