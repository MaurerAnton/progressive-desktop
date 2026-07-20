// src/ui/emoji_picker.hpp — simple emoji picker popup.
#pragma once
#include <QDialog>
#include <QGridLayout>
#include <QPushButton>
#include <QLineEdit>
#include <QVector>

namespace progressive::desktop {

class EmojiPicker : public QDialog {
    Q_OBJECT
public:
    explicit EmojiPicker(QWidget* parent = nullptr);

signals:
    void emojiSelected(const QString& emoji);

private slots:
    void onEmojiClicked(const QString& emoji);
    void onSearchChanged(const QString& text);

private:
    QLineEdit* searchEdit_;
    QGridLayout* grid_;
    QVector<QString> allEmojis_;
    QVector<QString> filteredEmojis_;

    void buildGrid();
    void populateEmojis();
};

} // namespace progressive::desktop
