// src/ui/dialogs/shortcuts_dialog.cpp
#include "shortcuts_dialog.hpp"
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QHeaderView>

namespace progressive::desktop {

ShortcutsDialog::ShortcutsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Keyboard Shortcuts");
    setMinimumSize(440, 520);
    auto* root = new QVBoxLayout(this);
    auto* table = new QTableWidget(this);
    table->setColumnCount(2);
    table->setHorizontalHeaderLabels({"Shortcut", "Action"});
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);

    struct { const char* key; const char* action; } rows[] = {
        {"Alt+Down", "Next room"},
        {"Alt+Up", "Previous room"},
        {"Ctrl+1..9", "Jump to Nth room"},
        {"Ctrl+Down", "Next message"},
        {"Ctrl+Up", "Previous message"},
        {"Ctrl+K", "Room switcher (filter by name)"},
        {"Ctrl+R", "Reply to selected message"},
        {"Ctrl+Shift+K", "Quick react (\xe2\x9d\xa4) on selected message"},
        {"Ctrl+L", "Focus room list"},
        {"Ctrl+Tab", "Next account"},
        {"Ctrl+Shift+C", "Smart Copy (sender + timestamp)"},
        {"Ctrl+V", "Paste image from clipboard"},
        {"Esc", "Close thread view / clear selection"},
        {"F11", "Toggle fullscreen"},
        {"F12", "Debug dump"},
        {"Double-click", "Toggle \xe2\x9d\xa4 reaction"},
    };
    table->setRowCount(sizeof(rows)/sizeof(rows[0]));
    for (int i = 0; i < (int)(sizeof(rows)/sizeof(rows[0])); ++i) {
        table->setItem(i, 0, new QTableWidgetItem(rows[i].key));
        table->setItem(i, 1, new QTableWidgetItem(rows[i].action));
    }
    root->addWidget(table);
}

} // namespace progressive::desktop
