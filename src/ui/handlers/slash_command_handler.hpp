#pragma once
#include <QObject>
#include <string>

namespace progressive::desktop {

class TimelineModel;
class AuthHandler;

class SlashCommandHandler : public QObject {
    Q_OBJECT
public:
    SlashCommandHandler(TimelineModel* timelineModel, AuthHandler* auth,
                        QObject* parent = nullptr);

public slots:
    void handleCommand(const std::string& cmd, const std::string& args);

private:
    TimelineModel* timelineModel_;
    AuthHandler* auth_;
};

} // namespace progressive::desktop
