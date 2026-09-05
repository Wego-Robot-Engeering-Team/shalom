#pragma once

// Main control station window.
//
//  +---------------------------------------------------------------+
//  | title bar: brand, badges, mode switch, emergency stop          |
//  +------+--------------------------------+-----------------------+
//  | nav  |                                |  context column       |
//  |      |          map (always)          |  (the nav switches    |
//  | ...  |                                |   only this)          |
//  |      +--------------------------------+-----------------------+
//  | 87%  |  event log (always)                                    |
//  +------+--------------------------------------------------------+
//
// What stays on screen in every mode: the map, the emergency stop, the battery
// and pose summary, and the event log. Losing sight of where the robot is
// while adjusting something else is how incidents happen, so navigation only
// swaps the context column.
//
// The manual jog controls appear only in manual mode. They are meaningless
// while the robot is driving itself, and showing disabled controls just spends
// screen space.

#include <QMainWindow>

#include "sim/SimRobot.h"
#include "panels/LocationPanel.h"
#include "widgets/MapCard.h"
#include "widgets/NavRail.h"

class QLabel;
class QPushButton;
class QStackedWidget;
class QTimer;

namespace gcs::diag {
class LogStore;
}

namespace gcs::map {
class MapView;
}

namespace gcs::ui {

class AlertFrame;
class ArmPanel;
class Badge;
class EStopButton;
class DiagnosticsPanel;
class EventLogPanel;
class SettingsDialog;
class MapCard;
class StatusPanel;
class TeleopPanel;
class WaypointPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(bool simMode = true, QWidget *parent = nullptr);

    /// Rebuilds the stylesheet and repaints everything that draws itself.
    void applyTheme(const QString &name);

    /// Selects the context column. Public so that a screenshot run can target
    /// a specific view.
    void showView(NavItem item);

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override;

    /// Fits the map on first show. The constructor cannot do it: the viewport
    /// has no final size until the layout has run, so fitting there leaves the
    /// map scaled to a stale rectangle.
    void showEvent(QShowEvent *ev) override;

private:
    QWidget *buildTopBar();
    QWidget *buildContextColumn();
    QWidget *buildDriveContext();
    QWidget *buildLocationsContext();
    QWidget *buildArmContext();
    QWidget *buildCaptureContext();
    QWidget *buildDiagnosticsContext();
    void wireSignals();

    void engageEstop();
    void releaseEstop();
    void setMode(const QString &mode);
    void navigate(NavItem item);
    void openSettings();

    /// Writes a log entry tagged with the signed-in operator, so the event log
    /// works as the audit trail the warranty period relies on.
    void logAction(const QString &code, QVariantMap detail = {});
    void onMissionStateChanged(gcs::sim::MissionState state);

    /// Records the current robot pose as a location of the given kind, after
    /// validating it. Rejections and low-confidence captures are logged with
    /// their reason so a bad waypoint can be traced later.
    void captureLocation(const QString &kind);

    void startSimulation();
    void tick();

    gcs::diag::LogStore *log_ = nullptr;

    NavRail *nav_ = nullptr;
    QStackedWidget *context_ = nullptr;
    MapCard *map_ = nullptr;

    StatusPanel *status_ = nullptr;
    TeleopPanel *teleop_ = nullptr;
    QWidget *teleopHost_ = nullptr;
    EventLogPanel *events_ = nullptr;
    WaypointPanel *waypoints_ = nullptr;
    ArmPanel *arm_ = nullptr;
    LocationPanel *locations_ = nullptr;
    DiagnosticsPanel *diagnostics_ = nullptr;
    SettingsDialog *settings_ = nullptr;

    EStopButton *estop_ = nullptr;
    AlertFrame *alert_ = nullptr;
    Badge *linkBadge_ = nullptr;
    Badge *missionBadge_ = nullptr;
    QPushButton *autoBtn_ = nullptr;
    QPushButton *manualBtn_ = nullptr;
    QPushButton *themeBtn_ = nullptr;
    QPushButton *settingsBtn_ = nullptr;
    Badge *userBadge_ = nullptr;

    /// What the map click should produce once placed: empty means a goal pose.
    QString pendingPlacementKind_;

    RobotSnapshot snapshot_;
    QVariantMap dock_;
    QVariantMap home_;

    bool simMode_ = true;
    bool didInitialFit_ = false;

    /// Stands in for the bridge until BridgeClient exists. Commands go here
    /// and telemetry comes back, so the whole interface is exercised for real.
    gcs::sim::SimRobot *robot_ = nullptr;
    gcs::sim::MapData mapData_;
    QTimer *timer_ = nullptr;
};

}  // namespace gcs::ui
