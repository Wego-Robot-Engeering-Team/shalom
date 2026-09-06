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


namespace hmi::ui {

class NavButton;

/// What the context column shows. The map, the emergency stop and the battery
/// strip in the top bar are not part of this: they stay on screen in every
/// mode, because losing sight of where the robot is while adjusting something
/// else is how incidents happen.
///
/// The event log used to be pinned below the column. It is a view of its own
/// now: kept always-visible it was a few cramped rows, and anything worth
/// interrupting the operator for already arrives as a toast and is filed in
/// the notification bell.
///
/// The order here is the display order.
enum class NavItem {
    Drive,        ///< mission progress and manual jog
    Locations,    ///< teach and edit waypoints, dock and home
    Arm,          ///< FR3 posture control
    Capture,      ///< capture control, preview and metadata
    Diagnostics,  ///< link health, sensor health, controller load
    Data,         ///< browsing and downloading past inspections from the share
    Events,       ///< the event log in full
};

class NavRail : public QWidget {
    Q_OBJECT
public:
    explicit NavRail(QWidget *parent = nullptr);

    void setCurrent(NavItem item);
    NavItem current() const { return current_; }

    /// Compact always-visible summary at the foot of the rail, so battery and
    /// pose stay readable whichever context column is showing.

    /// Badge count drawn on the diagnostics item; 0 hides it.
    void setDiagnosticsAlerts(int count);

    /// Unseen warnings and above, shown on the log button. The log is no
    /// longer pinned on screen, so this is how the operator knows there is
    /// something in it worth opening.
    void setEventAlerts(int count);

signals:
    void navigated(NavItem item);

private:
    QList<NavButton *> buttons_;
    NavButton *diagnosticsButton_ = nullptr;
    NavButton *eventsButton_ = nullptr;
    NavItem current_ = NavItem::Drive;
};

}  // namespace hmi::ui
