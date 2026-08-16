#pragma once

#include "pch.h"

// Application-wide visual styling.
//
// Qt style sheets cannot reference palette roles -- there is no palette(base)
// function -- so any colour written into one is fixed at author time and wrong
// in the other colour scheme. The sheet is therefore generated from the live
// QPalette, and regenerated when the system switches between light and dark.

// Builds the style sheet for a given palette. Pure, so it can be tested
// against both a light and a dark palette without a running application.
QString ThemeStyleSheet(const QPalette &palette);

// Applies the sheet and keeps it in step with the system colour scheme.
void InstallTheme(QApplication *app);

// Muted foreground for secondary columns, derived from the palette.
QColor SecondaryTextColor(const QPalette &palette);

// Slightly smaller font for secondary columns.
QFont SecondaryFont(const QFont &base);
