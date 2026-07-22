// tests/fake_client.hpp — standalone fake client for handler tests.
#pragma once
#include <string>
#include <QString>

namespace progressive::desktop {

struct FakeResult {
    bool ok = false;
    std::string data;
    int httpStatus = 0;
};

class FakeClient {
public:
    bool isLoggedIn() const { return true; }

    FakeResult sendReaction(const std::string& roomId,
                            const std::string& eventId,
                            const std::string& emoji) {
        (void)roomId; (void)eventId;
        lastReaction_ = QString::fromStdString(emoji);
        return {true, "mock_reaction_event_id", 200};
    }

    FakeResult sendMessage(const std::string& roomId,
                           const std::string& body,
                           const std::string& msgtype = "m.text",
                           const std::string& threadRoot = "",
                           const std::string& replyTo = "",
                           const std::string& formattedBody = "",
                           const std::string& format = "") {
        (void)roomId; (void)msgtype; (void)threadRoot;
        (void)replyTo; (void)formattedBody; (void)format;
        lastSentBody_ = QString::fromStdString(body);
        return {true, "mock_msg_event_id", 200};
    }

    QString lastReaction_;
    QString lastSentBody_;
};

} // namespace
