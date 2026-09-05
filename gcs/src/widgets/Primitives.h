#pragma once

// Shared layout primitives: Card, Badge, separator, form row.
//
// Styling is driven entirely by the global stylesheet keyed on objectName.
// Inline setStyleSheet() is avoided so that switching themes does not require
// repainting each widget by hand.

#include <QFrame>
#include <QLabel>

class QHBoxLayout;
class QVBoxLayout;

namespace gcs::ui {

/// Standard panel container: an optional title header above a padded body.
/// Content goes into body(); status widgets go into addHeaderWidget().
class Card : public QFrame {
    Q_OBJECT
public:
    explicit Card(const QString &title = {}, QWidget *parent = nullptr, bool padded = true);

    QVBoxLayout *body() const { return body_; }

    /// Appends a widget to the right-hand side of the header. Only valid on a
    /// card constructed with a title.
    void addHeaderWidget(QWidget *w);

private:
    QVBoxLayout *body_ = nullptr;
    QWidget *header_ = nullptr;
    QHBoxLayout *headerLayout_ = nullptr;
};

/// Small status pill. tone is one of: neutral, ok, warn, danger, info.
///
/// Reserve badges for state that actually changes. Using them for static
/// labels makes the screen colourful and, as a result, makes real transitions
/// harder to notice.
class Badge : public QLabel {
    Q_OBJECT
public:
    explicit Badge(const QString &text = {}, const QString &tone = QStringLiteral("neutral"),
                   QWidget *parent = nullptr);
    void setTone(const QString &tone);
    void set(const QString &text, const QString &tone);
};

class HLine : public QFrame {
    Q_OBJECT
public:
    explicit HLine(QWidget *parent = nullptr);
};

/// Muted caption used above a group of controls.
QLabel *sectionLabel(const QString &text);

/// Monospaced value display, so digits do not shift as the value changes.
QLabel *readout(const QString &text = QStringLiteral("—"), bool large = false);

/// A "label - widget" row with a fixed label column, for consistent forms.
QWidget *fieldRow(const QString &label, QWidget *widget, int labelWidth = 58);

}  // namespace gcs::ui
