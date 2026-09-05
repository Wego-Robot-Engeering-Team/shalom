#pragma once

// Left navigation rail.
//
// The control station covers several distinct jobs - driving, teaching
// locations, posing the arm, capturing, diagnostics - and cramming them onto
// one screen makes every one of them harder to read. The rail switches between
// task-focused views instead.
//
// The emergency stop and the mode switch deliberately stay in the title bar
// rather than living in a view: the statement of work requires the stop to be
// visible at all times (2.2.7 [5]).
//
// Icons are drawn with QPainter rather than shipped as an icon font. A handful
// of simple line glyphs is less machinery than a font dependency, and they
// follow the theme without needing recoloured assets.

#include <QList>
#include <QWidget>

class QLabel;

namespace gcs::ui {

class BatteryRing;
class NavButton;

/// What the context column shows. The map, the emergency stop, the battery
/// summary and the recent-event strip are not part of this: they stay on
/// screen in every mode, because losing sight of where the robot is while
/// adjusting something else is how incidents happen.
///
/// The order here is the display order.
enum class NavItem {
    Drive,        ///< mission progress and manual jog
    Locations,    ///< teach and edit waypoints, dock and home
    Arm,          ///< FR3 posture control
    Capture,      ///< capture control, preview and metadata
    Diagnostics,  ///< event log, transport health, code catalog
};

class NavRail : public QWidget {
    Q_OBJECT
public:
    explicit NavRail(QWidget *parent = nullptr);

    void setCurrent(NavItem item);
    NavItem current() const { return current_; }

    /// Compact always-visible summary at the foot of the rail, so battery and
    /// pose stay readable whichever context column is showing.
    void setBattery(double socPercent);
    void setPoseText(const QString &text);

    /// Badge count drawn on the diagnostics item; 0 hides it.
    void setDiagnosticsAlerts(int count);

signals:
    void navigated(NavItem item);

private:
    QList<NavButton *> buttons_;
    NavButton *diagnosticsButton_ = nullptr;
    BatteryRing *battery_ = nullptr;
    QLabel *pose_ = nullptr;
    NavItem current_ = NavItem::Drive;
};

}  // namespace gcs::ui
