// src/ui/network_log_dialog.hpp — view HTTP request/response log.
#pragma once
#include <QDialog>
#include <QTextEdit>
#include <QPushButton>

namespace progressive::desktop {

class NetworkLogDialog : public QDialog {
    Q_OBJECT
public:
    explicit NetworkLogDialog(QWidget* parent = nullptr);

private slots:
    void onRefresh();
    void onClear();

private:
    QTextEdit* logView_;
    QPushButton* refreshBtn_;
    QPushButton* clearBtn_;
    QPushButton* closeBtn_;
};

} // namespace progressive::desktop
