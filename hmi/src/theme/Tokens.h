#pragma once

// Design tokens: two palettes (dark / light) with runtime switching.
//
// Design stance
//   - No shadows or glows. Surfaces are separated by hairline borders only.
//   - Small corner radii (3-6 px). Heavy rounding reads as a toy, not a tool.
//   - One restrained blue accent. Semantic colors are desaturated so that a
//     status change is what catches the eye, not the chrome.
//   - All numeric readouts use a monospaced face. Values are unreadable when
//     digit positions shift between frames.
//
// The four waypoint status colors (done / current / pending / error) are
// mandated by the statement of work, section 2.2.7 [1] (2). Per-theme values
// may be tuned, but the color-to-meaning mapping must not change: it is an
// acceptance-test item.
//
// NOTE ON NAMING: the accessor is `colors()`, not `palette()`. QWidget already
// has a `palette()` member, so inside any widget subclass an unqualified
// `palette()` silently resolves to Qt's and the theme is ignored. Avoiding the
// collision entirely is safer than relying on every call site to qualify it.

#include <QFont>
#include <QLatin1String>
#include <QString>

namespace hmi::theme {

struct Colors {
    QLatin1String name;

    // Surface layers, back to front.
    QLatin1String bg;
    QLatin1String surface;
    QLatin1String surfaceHi;
    QLatin1String surfaceHover;
    QLatin1String overlay;

    // Borders.
    QLatin1String border;
    QLatin1String borderHi;

    // Text.
    QLatin1String text;
    QLatin1String textDim;
    QLatin1String textMute;
    QLatin1String textOnAccent;

    // Accent and semantic colors.
    QLatin1String accent;
    QLatin1String accentHi;
    QLatin1String accentLo;
    QLatin1String success;
    QLatin1String warning;
    QLatin1String danger;
    QLatin1String dangerHi;
    QLatin1String dangerLo;

    // Waypoint status colors mandated by the statement of work.
    QLatin1String wpDone;     ///< done - green
    QLatin1String wpCurrent;  ///< current - blue
    QLatin1String wpTodo;     ///< pending - grey
    QLatin1String wpError;    ///< error - red

    // Map layers.
    QLatin1String mapFree;
    QLatin1String mapOccupied;
    QLatin1String mapUnknown;
    QLatin1String plan;    ///< Nav2 planned path (solid blue)
    QLatin1String trail;   ///< travelled path (dashed grey)
    QLatin1String tag;     ///< AprilTag marker
    QLatin1String dock;    ///< charging station
    QLatin1String home;    ///< start position

    bool isDark() const { return name == QLatin1String("dark"); }
};

/// 4 px grid. Every spacing value is a multiple of these.
namespace metrics {
inline constexpr int s1 = 4;
inline constexpr int s2 = 8;
inline constexpr int s3 = 12;
inline constexpr int s4 = 16;
inline constexpr int s5 = 24;
inline constexpr int s6 = 32;

inline constexpr int rSm = 3;
inline constexpr int rMd = 5;
inline constexpr int rLg = 6;

// Raised alongside the type scale. Left at the old values, the taller text
// was clipped inside every control.
/// Top bar height. The navigation rail's brand block matches it so that the
/// divider under the logo lands in the middle of the gap between the bar and
/// the body - two columns that start at the same y should also break at the
/// same y.
inline constexpr int topBarH = 76;

inline constexpr int rowH = 30;
inline constexpr int ctlH = 33;
inline constexpr int ctlHSm = 27;
}  // namespace metrics

/// Font stacks.
///
/// Bundling Pretendard under resources/fonts makes rendering identical on
/// Windows and Ubuntu. The Windows default Korean face (Malgun Gothic) has
/// dated spacing and hinting and noticeably degrades the interface, so the
/// delivery build must ship the bundled font.
/// Pretendard is licensed under SIL OFL 1.1 (bundling and redistribution
/// permitted; include the license file in the escrow package).
namespace type {
inline constexpr auto ui =
    "\"Pretendard Variable\", \"Pretendard\", \"Inter\", -apple-system, "
    "\"Segoe UI\", \"Malgun Gothic\", sans-serif";
inline constexpr auto mono =
    "\"JetBrains Mono\", \"SF Mono\", \"Consolas\", \"D2Coding\", monospace";

// Sized for a screen read while standing in a depot, not from a desk. The
// original desktop-sized scale was reported as too small on site, so every
// step is one point larger; the settings dialog scales the whole set further.
inline constexpr int xs = 11;
inline constexpr int sm = 12;
inline constexpr int md = 13;
inline constexpr int lg = 15;
inline constexpr int xl = 18;
inline constexpr int xxl = 25;
}  // namespace type

extern const Colors kDark;
extern const Colors kLight;

/// Multiplier applied to every font size when the stylesheet is built.
/// Control rooms are frequently viewed from further away than a desk, and the
/// operator is often not the person who set the machine up, so this is a real
/// operational need rather than a preference.
double uiScale();
void setUiScale(double scale);

/// Font size in points after the UI scale is applied.
int scaled(int basePoints);

/// The active color set. Custom-painted widgets must call this inside their
/// paint handler (not cache it) so that a theme switch takes effect without
/// restarting the application.
const Colors &colors();

/// Switches the active theme. Accepts "dark" or "light"; anything else is
/// treated as "light".
const Colors &setTheme(const QString &name);
const Colors &toggleTheme();

/// First family name from the monospace stack, for use with QFont(family).
/// Monospaced font with the whole fallback list attached.
///
/// Returning just the first family name was wrong on every machine that does
/// not have it installed: the stylesheet honours the full list, but code that
/// builds a QFont by name does not, so custom-painted digits fell back to a
/// proportional face and shifted as values changed. JetBrains Mono is not
/// present on a stock Windows or Ubuntu.
QFont monoFont(int pointSize);

}  // namespace hmi::theme
