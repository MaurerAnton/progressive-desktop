// src/ui/emoji_picker.hpp — emoji picker popup with search.
#pragma once
#include <QDialog>
#include <QGridLayout>
#include <QPushButton>
#include <QLineEdit>
#include <QScrollArea>
#include <QVector>
#include <QHash>

namespace progressive::desktop {

struct EmojiEntry {
    QString emoji;
    QString name;       // short name (e.g. "candle")
    QString keywords;   // space-separated search terms
};

class EmojiPicker : public QDialog {
    Q_OBJECT
public:
    explicit EmojiPicker(QWidget* parent = nullptr);

signals:
    void emojiSelected(const QString& emoji);

private slots:
    void onSearchChanged(const QString& text);
    void onEmojiClicked(const QString& emoji);

private:
    QLineEdit* searchEdit_;
    QScrollArea* scrollArea_;
    QGridLayout* grid_;
    QVector<EmojiEntry> allEmojis_;
    QVector<QPushButton*> buttons_;  // built once, show/hide on search

    void populateEmojis();
    void buildGrid();
};

} // namespace progressive::desktop
