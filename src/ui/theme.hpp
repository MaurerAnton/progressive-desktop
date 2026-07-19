// src/ui/theme.hpp — dark theme for progressive-desktop.
//
// Applied to QApplication before any widgets are constructed. Sets the
// Fusion style (cross-platform, consistent on PineTab 2 / Linux desktop)
// with a dark QPalette.
//
// All UI colors in timeline_view.cpp etc. are picked to look good on the
// dark palette. To switch to a light theme later, add a settings option
// and call applyLightTheme() instead — but dark is the default.

#pragma once

class QApplication;

namespace progressive::desktop {

// Apply the default dark theme. Call once after QApplication is constructed,
// before any widgets are shown.
void applyDarkTheme(QApplication& app);

} // namespace progressive::desktop
