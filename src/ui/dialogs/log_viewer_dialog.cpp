// src/ui/dialogs/log_viewer_dialog.cpp
#include "log_viewer_dialog.hpp"

#include "core/debug_log.hpp"
#include "../shared/theme.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QString>
#include <QVBoxLayout>
#include <algorithm>

namespace progressive::desktop {

namespace {
QColor channelColor(LogChannel ch) {
    switch (ch) {
        case LogChannel::GUI:  return QColor("#8f7ae5");
        case LogChannel::SYNC: return QColor("#5ab0e0");
        case LogChannel::E2EE: return QColor("#e05a5a");
        case LogChannel::NET:  return QColor("#5ae0a0");
        case LogChannel::MEM:  return QColor("#e0b85a");
        case LogChannel::DBG:  return Design::textColor;
    }
    return Design::textColor;
}
}  // namespace

LogViewerDialog::LogViewerDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Log viewer");
    resize(760, 520);

    auto* root = new QVBoxLayout(this);

    auto* hint = new QLabel(
        "Live view of the last 2000 log lines (all channels). "
        "Check/uncheck channels, type to filter. "
        "Copy: select lines (or none = all visible) and press Copy. "
        "Opened via Settings menu \u2192 Log viewer.", this);
    hint->setWordWrap(true);
    hint->setStyleSheet("color:" + Design::mutedTextColor.name() + "; font-size:9pt;");
    root->addWidget(hint);

    auto* filterRow = new QHBoxLayout;
    filterEdit_ = new QLineEdit(this);
    filterEdit_->setPlaceholderText("Filter text…");
    filterRow->addWidget(new QLabel("Filter:", this));
    filterRow->addWidget(filterEdit_, 1);
    root->addLayout(filterRow);

    auto* channelRow = new QHBoxLayout;
    channelRow->addWidget(new QLabel("Channels:", this));
    for (auto* name : {"GUI", "SYNC", "E2EE", "NET", "MEM", "DBG"}) {
        auto* cb = new QCheckBox(name, this);
        cb->setChecked(true);
        channelBoxes_.push_back(cb);
        channelRow->addWidget(cb);
    }
    channelRow->addStretch();
    root->addLayout(channelRow);

    list_ = new QListWidget(this);
    list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    root->addWidget(list_, 1);

    auto* bottomRow = new QHBoxLayout;
    copyBtn_ = new QPushButton("Copy", this);
    copyBtn_->setToolTip("Copy selected lines — or all visible lines if nothing is selected.");
    bottomRow->addWidget(copyBtn_);
    bottomRow->addStretch();
    root->addLayout(bottomRow);

    connect(copyBtn_, &QPushButton::clicked, this, &LogViewerDialog::onCopy);
    connect(filterEdit_, &QLineEdit::textChanged, this, [this](const QString&) { rebuild(); });
    for (auto* cb : channelBoxes_)
        connect(cb, &QCheckBox::toggled, this, [this](bool) { rebuild(); });

    timer_.setInterval(200);
    connect(&timer_, &QTimer::timeout, this, &LogViewerDialog::refresh);
    timer_.start();
}

void LogViewerDialog::refresh() {
    bool atBottom = !list_->verticalScrollBar() ||
                    list_->verticalScrollBar()->value() >=
                        list_->verticalScrollBar()->maximum() - 20;
    bool appended = false;

    std::set<LogChannel> on;
    for (size_t i = 0; i < channelBoxes_.size(); ++i)
        if (channelBoxes_[i]->isChecked()) on.insert(static_cast<LogChannel>(i));
    QString filter = filterEdit_->text().toLower();

    for (const auto& line : snapshotLogRing()) {
        if (shownSeqs_.count(line.seq)) continue;
        shownSeqs_.insert(line.seq);
        if (!on.count(line.channel)) continue;
        QString text = QString::fromStdString(line.text);
        if (!filter.isEmpty() && !text.toLower().contains(filter)) continue;
        auto* item = new QListWidgetItem(QString("[%1] %2")
                .arg(logChannelName(line.channel)).arg(text));
        item->setForeground(channelColor(line.channel));
        list_->addItem(item);
        appended = true;
    }
    if (appended && atBottom)
        list_->verticalScrollBar()->setValue(list_->verticalScrollBar()->maximum());
}

void LogViewerDialog::onCopy() {
    QStringList lines;
    auto selected = list_->selectedItems();
    if (selected.isEmpty()) {
        for (int i = 0; i < list_->count(); ++i)
            lines << list_->item(i)->text();
    } else {
        for (auto* item : selected)
            lines << item->text();
    }
    QApplication::clipboard()->setText(lines.join('\n'));
    copyBtn_->setText("Copied!");
    QTimer::singleShot(1500, this, [this]() { copyBtn_->setText("Copy"); });
}

void LogViewerDialog::rebuild() {
    list_->clear();
    shownSeqs_.clear();

    std::set<LogChannel> on;
    for (size_t i = 0; i < channelBoxes_.size(); ++i)
        if (channelBoxes_[i]->isChecked()) on.insert(static_cast<LogChannel>(i));
    QString filter = filterEdit_->text().toLower();

    for (const auto& line : snapshotLogRing()) {
        shownSeqs_.insert(line.seq);
        if (!on.count(line.channel)) continue;
        QString text = QString::fromStdString(line.text);
        if (!filter.isEmpty() && !text.toLower().contains(filter)) continue;
        auto* item = new QListWidgetItem(QString("[%1] %2")
                .arg(logChannelName(line.channel)).arg(text));
        item->setForeground(channelColor(line.channel));
        list_->addItem(item);
    }
    list_->verticalScrollBar()->setValue(list_->verticalScrollBar()->maximum());
}

}  // namespace progressive::desktop
