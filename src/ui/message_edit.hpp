// src/ui/message_edit.hpp — input box for sending messages.
// Composite widget: [📎 attach] [😊 emoji] [ QTextEdit     ]

#pragma once

#include <QWidget>
#include <QTextEdit>
#include <QPushButton>
#include <string>

namespace progressive::desktop {

class MessageEdit : public QWidget {
    Q_OBJECT

public:
    explicit MessageEdit(QWidget* parent = nullptr);

    // Access the underlying text edit for focus/clear.
    QTextEdit* textEdit() { return textEdit_; }
    QString toPlainText() const { return textEdit_->toPlainText(); }
    void clear() { textEdit_->clear(); }
    void setFocus() { textEdit_->setFocus(); }
    void show() { QWidget::show(); textEdit_->show(); }
    void hide() { QWidget::hide(); }

signals:
    void sendMessage(const std::string& body);
    void slashCommand(const std::string& command, const std::string& args);
    void attachFileRequested();
    void emojiPickerRequested();
    void quickReact(const QString& emoji);

private slots:
    void onAttachClicked();
    void onEmojiClicked();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    QTextEdit* textEdit_ = nullptr;
    QPushButton* attachBtn_ = nullptr;
    QPushButton* emojiBtn_ = nullptr;

    void setupUi();
};

} // namespace progressive::desktop

