// src/ui/room/room_data_loader.hpp — async room data loading.
#pragma once
#include <QObject>
#include <string>
#include <vector>
#include <functional>
#include <memory>

namespace progressive::desktop {

using LifeToken = std::shared_ptr<bool>;

class MatrixClient;
class SessionStore;
class RoomListModel;
class TimelineModel;
struct MemberInfo;

class RoomDataLoader : public QObject {
    Q_OBJECT
public:
    RoomDataLoader(std::shared_ptr<MatrixClient> client, std::shared_ptr<SessionStore> store,
                   QObject* parent = nullptr);

    void setClient(std::shared_ptr<MatrixClient> c) { client_ = std::move(c); }
    void setSessionStore(std::shared_ptr<SessionStore> s) { store_ = std::move(s); }

    void loadHistory(const std::string& roomId, TimelineModel* model,
                     LifeToken token, std::function<void(int, const std::string&)> callback);

    void loadMembers(const std::string& roomId, LifeToken token,
                     const std::vector<std::string>& userIds,
                     std::function<void(std::vector<MemberInfo>)> callback);

    void batchLoadRoomStates(RoomListModel* model, LifeToken token);

private:
    std::shared_ptr<MatrixClient> client_;
    std::shared_ptr<SessionStore> store_;
};

} // namespace progressive::desktop
