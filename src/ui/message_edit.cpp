// src/ui/message_edit.cpp

#include "message_edit.hpp"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QFont>
#include <QFontDatabase>
#include <QMenu>
#include <QAbstractItemView>
#include <QScrollBar>

namespace progressive::desktop {

MessageEdit::MessageEdit(QWidget* parent)
    : QWidget(parent) {
    setupUi();
}

void MessageEdit::setupUi() {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    // Attach button — opens file picker
    attachBtn_ = new QPushButton("📎", this);
    attachBtn_->setFixedSize(36, 36);
    attachBtn_->setToolTip("Attach file (image, document, PDF)");
    attachBtn_->setFocusPolicy(Qt::NoFocus);

    // Try to set emoji font for the buttons — use bundled OpenMoji if no
    // system emoji font is available.
    QFont btnFont = attachBtn_->font();
    btnFont.setPointSize(16);
    QStringList emojiFonts = {"Noto Color Emoji", "Apple Color Emoji", "Segoe UI Emoji", "OpenMoji Color"};
    QStringList families = QFontDatabase::families();
    for (const QString& f : emojiFonts) {
        if (families.contains(f, Qt::CaseInsensitive)) { btnFont.setFamily(f); break; }
    }
    attachBtn_->setFont(btnFont);

    // Emoji button — opens emoji picker for message input
    emojiBtn_ = new QPushButton("😊", this);
    emojiBtn_->setFixedSize(36, 36);
    emojiBtn_->setToolTip("Insert emoji into message");
    emojiBtn_->setFocusPolicy(Qt::NoFocus);
    emojiBtn_->setFont(btnFont);

    // Quick-react button — opens popup of recent emojis for fast reaction
    auto* reactBtn = new QPushButton("😍", this);
    reactBtn->setFixedSize(36, 36);
    reactBtn->setToolTip("Quick react to last message");
    reactBtn->setFocusPolicy(Qt::NoFocus);
    reactBtn->setFont(btnFont);

    QStringList quickEmojis = {"❤", "👍", "😂", "😮", "🎉", "👎"};
    connect(reactBtn, &QPushButton::clicked, this, [this, quickEmojis]() {
        QMenu menu;
        for (const QString& e : quickEmojis) {
            auto* a = menu.addAction(e);
            connect(a, &QAction::triggered, this, [this, e]() { emit quickReact(e); });
        }
        menu.exec(QCursor::pos());
    });

    // Text edit
    textEdit_ = new QTextEdit(this);
    textEdit_->setAcceptRichText(false);
    textEdit_->setPlaceholderText("Type a message — Enter to send, Shift+Enter for newline");
    textEdit_->setMaximumHeight(120);
    textEdit_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    textEdit_->installEventFilter(this);

    // Subclass key handling via eventFilter (since we can't override keyPressEvent
    // of QTextEdit directly when using composition)
    layout->addWidget(attachBtn_);
    layout->addWidget(emojiBtn_);
    layout->addWidget(reactBtn);
    layout->addWidget(textEdit_, 1);  // stretch factor 1 = fills width

    connect(attachBtn_, &QPushButton::clicked, this, &MessageEdit::onAttachClicked);
    connect(emojiBtn_, &QPushButton::clicked, this, &MessageEdit::onEmojiClicked);
}

void MessageEdit::onAttachClicked() {
    emit attachFileRequested();
}

void MessageEdit::onEmojiClicked() {
    emit emojiPickerRequested();
}

bool MessageEdit::eventFilter(QObject* obj, QEvent* event) {
    if (obj == textEdit_ && event->type() == QEvent::KeyPress) {
        auto* e = static_cast<QKeyEvent*>(event);
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            if (!(e->modifiers() & Qt::ShiftModifier)) {
                // Send
                QString text = textEdit_->toPlainText().trimmed();
                if (!text.isEmpty()) {
                    if (text.startsWith("/")) {
                        int space = text.indexOf(' ');
                        QString cmd = (space == -1) ? text.mid(1) : text.mid(1, space - 1);
                        QString args = (space == -1) ? QString() : text.mid(space + 1);
                        emit slashCommand(cmd.toStdString(), args.toStdString());
                    } else {
                        emit sendMessage(text.toStdString());
                    }
                    textEdit_->clear();
                }
                e->accept();
                return true;
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}

void MessageEdit::setMembers(const QStringList& names) {
    if (!mentionModel_) {
        mentionModel_ = new QStringListModel(this);
        mentionCompleter_ = new QCompleter(mentionModel_, this);
        mentionCompleter_->setCompletionMode(QCompleter::PopupCompletion);
        mentionCompleter_->setCaseSensitivity(Qt::CaseInsensitive);
        mentionCompleter_->setFilterMode(Qt::MatchContains);
    }
    mentionModel_->setStringList(names);
    if (completerSetup_) return;
    completerSetup_ = true;
    // Manual trigger: when @ is typed, show popup
    connect(textEdit_, &QTextEdit::textChanged, this, [this]() {
        QString text = textEdit_->toPlainText();
        int cursor = textEdit_->textCursor().position();
        // Find the @mention fragment
        int atPos = text.lastIndexOf('@', cursor - 1);
        if (atPos >= 0 && (atPos == 0 || text[atPos - 1].isSpace())) {
            QString fragment = text.mid(atPos + 1, cursor - atPos - 1);
            if (fragment.size() >= 0) {
                mentionCompleter_->setCompletionPrefix(fragment);
                if (mentionCompleter_->completionCount() > 0) {
                    QRect cr = textEdit_->cursorRect();
                    cr.setWidth(mentionCompleter_->popup()->sizeHintForColumn(0)
                                + mentionCompleter_->popup()->verticalScrollBar()->sizeHint().width() + 4);
                    mentionCompleter_->complete(cr);
                    return;
                }
            }
        }
        mentionCompleter_->popup()->hide();
    });
    // Insert selected name
    connect(mentionCompleter_, QOverload<const QString&>::of(&QCompleter::activated),
            this, [this](const QString& name) {
        QTextCursor c = textEdit_->textCursor();
        QString text = textEdit_->toPlainText();
        int cursor = c.position();
        int atPos = text.lastIndexOf('@', cursor - 1);
        if (atPos >= 0) {
            c.setPosition(atPos);
            c.setPosition(cursor, QTextCursor::KeepAnchor);
            c.insertText("@" + name + " ");
            textEdit_->setTextCursor(c);
        }
    });
}

} // namespace progressive::desktop

