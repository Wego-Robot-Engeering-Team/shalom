#pragma once

// Small painted glyph buttons for the top bar.
//
// Settings is sliders, not a gear. A gear and a sun are both a ring with eight
// spokes around it, and at 15 px in a toolbar the two were easy to confuse -
// which matters because one opens a dialog and the other repaints the app.
//
// The glyphs are drawn with QPainter rather than taken from a font or an emoji.
// An emoji renders as a different shape, and often in a different colour, on
// each platform, and the delivered build has to look the same on Windows and
// Ubuntu. Drawing them also lets them follow the theme without a second set of
// recoloured assets.

#include <QPushButton>

namespace hmi::ui {

class IconButton : public QPushButton {
    Q_OBJECT
public:
    enum class Glyph {
        Sliders,   ///< settings
        Sun,       ///< switch to the light theme
        Moon,      ///< switch to the dark theme
    };

    explicit IconButton(Glyph glyph, QWidget *parent = nullptr);

    /// Swaps the glyph in place, for a control whose meaning flips (the theme
    /// toggle shows where it will go, not where it is).
    void setGlyph(Glyph glyph);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    Glyph glyph_;
};

}  // namespace hmi::ui
