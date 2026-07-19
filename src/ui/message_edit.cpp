// src/ui/message_edit.cpp

#include "message_edit.hpp"

#include <QKeyEvent>

namespace progressive::desktop {

MessageEdit::MessageEdit(QWidget* parent)
    : QTextEdit(parent) {
    setAcceptRichText(false);
    setPlaceholderText("Type a message — Enter to send, Shift+Enter for newline");
    setMaximumHeight(100);
}

void MessageEdit::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        if (e->modifiers() & Qt::ShiftModifier) {
            // Newline
            QTextEdit::keyPressEvent(e);
            return;
        }
        // Send
        QString text = toPlainText().trimmed();
        if (!text.isEmpty()) {
            if (text.startsWith("/")) {
                // Slash command
                int space = text.indexOf(' ');
                QString cmd = (space == -1) ? text.mid(1) : text.mid(1, space - 1);
                QString args = (space == -1) ? QString() : text.mid(space + 1);
                emit slashCommand(cmd.toStdString(), args.toStdString());
            } else {
                emit sendMessage(text.toStdString());
            }
            clear();
        }
        e->accept();
        return;
    }
    QTextEdit::keyPressEvent(e);
}

} // namespace progressive::desktop
