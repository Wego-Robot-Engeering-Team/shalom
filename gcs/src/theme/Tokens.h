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

#include <QLatin1String>
#include <QString>

namespace gcs::theme {

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

inline constexpr int rowH = 28;
inline constexpr int ctlH = 30;
inline constexpr int ctlHSm = 24;
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

inline constexpr int xs = 10;
inline constexpr int sm = 11;
inline constexpr int md = 12;
inline constexpr int lg = 14;
inline constexpr int xl = 17;
inline constexpr int xxl = 24;
}  // namespace type

extern const Colors kDark;
extern const Colors kLight;

/// The active color set. Custom-painted widgets must call this inside their
/// paint handler (not cache it) so that a theme switch takes effect without
/// restarting the application.
const Colors &colors();

/// Switches the active theme. Accepts "dark" or "light"; anything else is
/// treated as "light".
const Colors &setTheme(const QString &name);
const Colors &toggleTheme();

/// First family name from the monospace stack, for use with QFont(family).
QString monoFamily();

}  // namespace gcs::theme
