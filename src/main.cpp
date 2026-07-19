// src/main.cpp — minimal Qt6 window for progressive-desktop Phase 0.
// Just opens a window so we can verify the toolchain + progressive_native link.
// Real UI starts in Phase 2.

#include <QApplication>
#include <QLabel>
#include <QWidget>
#include <QVBoxLayout>

#include "progressive/markdown.hpp"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("progressive-desktop");
    QApplication::setApplicationVersion("0.0.1");
    QApplication::setOrganizationName("progressive.chat");

    QWidget window;
    window.setWindowTitle("Progressive Chat — Desktop");
    window.resize(900, 600);

    auto* layout = new QVBoxLayout(&window);

    auto* title = new QLabel("<h2>Progressive Chat — Desktop</h2>");
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    // Smoke-test: call into progressive_native to verify the link works.
    // markdown.cpp is a Tier-A module — if this returns anything, we're linked.
    QString probe = QString::fromStdString(progressive::markdownToHtml("**hello** progressive"));
    auto* probe_label = new QLabel(probe.isEmpty()
        ? "progressive_native: linked (markdown returned empty for probe)"
        : "progressive_native: linked.\nMarkdown probe: " + probe);
    probe_label->setWordWrap(true);
    probe_label->setAlignment(Qt::AlignCenter);
    layout->addWidget(probe_label);

    auto* status = new QLabel("Phase 0 — toolchain OK.\nNext: sync engine + UI (phase 1-2).");
    status->setAlignment(Qt::AlignCenter);
    layout->addWidget(status);

    window.show();
    return app.exec();
}
