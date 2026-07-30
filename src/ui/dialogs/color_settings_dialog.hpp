// src/ui/dialogs/color_settings_dialog.hpp — runtime color customization.
#pragma once
#include <QDialog>
#include <QColor>
#include <vector>
#include <utility>
#include <string>

class QPushButton;

namespace progressive::desktop {

class ColorSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit ColorSettingsDialog(QWidget* parent = nullptr);

private slots:
    void onSave();

private:
    struct Row {
        std::string key;
        QColor* target;
        QPushButton* btn;
    };
    std::vector<Row> rows_;
    void addRow(const std::string& label, QColor* target,
                const std::string& qsettingsKey);
};

} // namespace progressive::desktop
