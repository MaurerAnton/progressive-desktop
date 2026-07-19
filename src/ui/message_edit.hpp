// src/ui/message_edit.hpp — input box for sending messages.

#pragma once

#include <QTextEdit>
#include <string>

namespace progressive::desktop {

class MessageEdit : public QTextEdit {
    Q_OBJECT

public:
    explicit MessageEdit(QWidget* parent = nullptr);

signals:
    void sendMessage(const std::string& body);
    void slashCommand(const std::string& command, const std::string& args);

protected:
    void keyPressEvent(QKeyEvent* e) override;
};

} // namespace progressive::desktop
