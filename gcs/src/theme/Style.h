#pragma once

// Global stylesheet generation.
//
// Exactly one stylesheet is installed on QApplication. Per-widget
// setStyleSheet() is deliberately avoided so that a theme switch only has to
// rebuild one string instead of hunting down every widget that painted itself.
//
// Dynamic state is expressed with Qt property selectors:
//     btn->setProperty("variant", "danger");
//     repolish(btn);

#include <QString>

class QWidget;

namespace gcs::theme {

struct Colors;

/// Formats a color as a QSS rgba() string.
///
/// WARNING: eight-digit hex in QSS is #AARRGGBB, not the CSS #RRGGBBAA. Writing
/// the alpha last puts it in the red channel instead, which silently yields a
/// completely different color (white + D9 renders as pale yellow). Every
/// translucent color therefore goes through this function.
QString rgba(const QString &hexColor, double alpha);

/// Re-applies the stylesheet to a widget. Qt does not re-evaluate property
/// selectors on its own, so this must be called after changing a property that
/// participates in a selector.
void repolish(QWidget *w);

/// Builds the global stylesheet. Passing nullptr uses the active theme.
QString buildQss(const Colors *pal = nullptr);

}  // namespace gcs::theme
