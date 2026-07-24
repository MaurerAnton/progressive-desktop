// src/ui/network_log_dialog.cpp
#include "network_log_dialog.hpp"
#include "core/http_client.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollBar>
#include <QTimer>
#include <sstream>

namespace progressive::desktop {

namespace {
inline constexpr int kNetLogW    = 750;
inline constexpr int kNetLogH    = 500;
inline constexpr int kLogRefresh = 2000;
inline constexpr int kScrollHysteresis = 10;
} // namespace

NetworkLogDialog::NetworkLogDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Network Log");
    resize(kNetLogW, kNetLogH);
    setModal(false);

    logView_ = new QTextEdit(this);
    logView_->setReadOnly(true);
    logView_->setStyleSheet(
        "QTextEdit{background:#0d0d0d; color:#ddd; font-family:monospace; font-size:11pt; "
        "border:1px solid #333;}");

    refreshBtn_ = new QPushButton("Refresh", this);
    clearBtn_ = new QPushButton("Clear", this);
    closeBtn_ = new QPushButton("Close", this);

    auto* btnRow = new QHBoxLayout;
    btnRow->addWidget(refreshBtn_);
    btnRow->addWidget(clearBtn_);
    btnRow->addStretch();
    auto* toggleAuto = new QPushButton("Auto", this);
    toggleAuto->setCheckable(true);
    toggleAuto->setChecked(true);
    toggleAuto->setToolTip("Auto-refresh every 2 seconds");
    btnRow->addWidget(toggleAuto);
    btnRow->addWidget(closeBtn_);

    auto* root = new QVBoxLayout(this);
    root->addWidget(new QLabel("Last 500 HTTP requests", this));
    root->addWidget(logView_);
    root->addLayout(btnRow);

    connect(refreshBtn_, &QPushButton::clicked, this, &NetworkLogDialog::onRefresh);
    connect(clearBtn_, &QPushButton::clicked, this, &NetworkLogDialog::onClear);
    connect(closeBtn_, &QPushButton::clicked, this, &QDialog::accept);

    // Auto-refresh timer
    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &NetworkLogDialog::onRefresh);
    connect(toggleAuto, &QPushButton::toggled, timer, [timer](bool on) {
        if (on) timer->start(2000);
        else timer->stop();
    });
    timer->start(2000);

    onRefresh();
}

void NetworkLogDialog::onRefresh() {
    auto entries = getHttpLog();
    int scrollPos = logView_->verticalScrollBar()->value();
    bool atBottom = (scrollPos >= logView_->verticalScrollBar()->maximum() - 10);

    QString html;
    html += "<html><body style='font-family:monospace;font-size:11pt;color:#ccc;'>";
    html += "<table cellspacing=0 cellpadding=2>";

    int i = 0;
    for (const auto& e : entries) {
        QString rowColor = (i++ % 2 == 0) ? "#1a1a1a" : "#141414";
        QString statusColor;
        if (e.statusCode >= 200 && e.statusCode < 300) statusColor = "#6c6";
        else if (e.statusCode >= 400) statusColor = "#f66";
        else if (e.statusCode > 0) statusColor = "#fc6";
        else statusColor = "#888";

        QString method = QString::fromStdString(e.method);
        QString methodColor;
        if (method == "GET") methodColor = "#6af";
        else if (method == "PUT") methodColor = "#fa6";
        else if (method == "POST") methodColor = "#6f6";
        else methodColor = "#ccc";

        QString url = QString::fromStdString(e.url).toHtmlEscaped();
        QString time = QString::number(e.elapsedMs) + "ms";
        QString size;
        if (e.responseBytes >= 1024 * 1024)
            size = QString::number(e.responseBytes / (1024 * 1024)) + " MB";
        else if (e.responseBytes >= 1024)
            size = QString::number(e.responseBytes / 1024) + " KB";
        else
            size = QString::number(e.responseBytes) + " B";

        QString error = QString::fromStdString(e.error);
        if (!error.isEmpty()) {
            error = "<span style='color:#f66'>" + error.toHtmlEscaped() + "</span>";
        }

        html += QString("<tr style='background:%1'>"
                        "<td><span style='color:%2'>%3</span></td>"
                        "<td style='color:#aaa'>%4</td>"
                        "<td><span style='color:%5'>%6</span></td>"
                        "<td style='color:#888'>%7</td>"
                        "<td style='color:#888'>%8</td>"
                        "<td>%9</td>"
                        "</tr>")
            .arg(rowColor, methodColor, method,
                 QString::number(e.statusCode > 0 ? e.statusCode : 0),
                 statusColor, size,
                 time,
                 url,
                 error);
    }
    html += "</table></body></html>";

    logView_->setHtml(html);

    if (atBottom) {
        logView_->verticalScrollBar()->setValue(logView_->verticalScrollBar()->maximum());
    } else {
        logView_->verticalScrollBar()->setValue(scrollPos);
    }
}

void NetworkLogDialog::onClear() {
    clearHttpLog();
    logView_->clear();
}

} // namespace progressive::desktop
