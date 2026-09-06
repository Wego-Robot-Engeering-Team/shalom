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

#include "robot/RobotLink.h"
#include "sim/SimRobot.h"
#include "panels/LocationPanel.h"
#include "widgets/MapCard.h"
#include "widgets/NavRail.h"
#include "widgets/Toast.h"

class QLabel;
class QPushButton;
class QStackedWidget;
class QTimer;

namespace gcs::diag {
class LogStore;
struct LogEntry;
}

namespace gcs::map {
class MapView;
}

namespace gcs::ui {

class AlertFrame;
class ArmPanel;
class CapturePanel;
class DataPanel;
class Badge;
class BatteryPill;
class NotificationBell;
class EStopButton;
class DiagnosticsPanel;
class EventLogPanel;
class SettingsDialog;
class MapCard;
class MissionPanel;
class StatusPanel;
class TeleopPanel;
class WaypointPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    /// Takes ownership of `link`. Passing the simulator or the real bridge
    /// client is the only difference between offline and live operation.
    explicit MainWindow(gcs::robot::RobotLink *link, QWidget *parent = nullptr);

    /// Rebuilds the stylesheet and repaints everything that draws itself.
    void applyTheme(const QString &name);

    /// Selects the context column. Public so that a screenshot run can target
    /// a specific view.
    void showView(NavItem item);

    /// Switches drive mode from outside the window. Used by the development
    /// screenshot options; the operator path goes through the top-bar buttons.
    void setDriveMode(const QString &mode) { setMode(mode); }

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
    QWidget *buildDataContext();
    /// Connects every signal, split by what the operator is touching.
    /// One 240-line function made it impossible to see whether a panel
    /// was wired at all - two panels were not.
    void wireSignals();
    /// Robot link and log: telemetry, connection, mission state, incoming map.
    void wireRobotSignals();
    /// Top bar: theme, settings, emergency stop and the drive-mode buttons.
    void wireChromeSignals();
    /// Map interactions: goal placement, point placement, point clicks.
    void wireMapSignals();
    /// Location teaching and the fixed dock/home points.
    void wireLocationSignals();
    /// Jog, arm, capture and stored-data panels.
    void wirePanelSignals();
    /// The inspection point list and mission start/pause/resume/stop.
    void wireMissionSignals();

    void engageEstop();
    void releaseEstop();
    void setMode(const QString &mode);
    void navigate(NavItem item);
    void openSettings();

    /// Writes a log entry tagged with the signed-in operator, so the event log
    /// works as the audit trail the warranty period relies on.
    void logAction(const QString &code, QVariantMap detail = {});
    void onMissionStateChanged(gcs::robot::MissionState state);
    void onTelemetry(const gcs::robot::Telemetry &tm);

    /// Raises a non-modal alert for entries the operator must not miss.
    /// Deliberately not a dialog: see widgets/Toast.h.
    void onLogAppended(const gcs::diag::LogEntry &entry);

    /// Shows what is known about a waypoint, including any captures already
    /// filed for it.
    void showWaypointInfo(const QString &id, const QPoint &globalPos);

    /// Records the current robot pose as a location of the given kind, after
    /// validating it. Rejections and low-confidence captures are logged with
    /// their reason so a bad waypoint can be traced later.
    void captureLocation(const QString &kind);

    void startSession();

    /// Opens today's JSONL log file and drops files past the retention window.
    /// Failure is reported into the log itself and is not fatal: an operator
    /// with no log file is still better off than one with no application.
    void startLogFile();
    static void pruneOldLogs(const QString &dir, int retentionDays);

    gcs::diag::LogStore *log_ = nullptr;

    NavRail *nav_ = nullptr;
    QStackedWidget *context_ = nullptr;
    MapCard *map_ = nullptr;

    BatteryPill *headerBattery_ = nullptr;
    NotificationBell *bell_ = nullptr;
    StatusPanel *status_ = nullptr;
    MissionPanel *mission_ = nullptr;
    TeleopPanel *teleop_ = nullptr;
    QWidget *teleopHost_ = nullptr;
    EventLogPanel *events_ = nullptr;
    WaypointPanel *waypoints_ = nullptr;
    ArmPanel *arm_ = nullptr;
    LocationPanel *locations_ = nullptr;
    CapturePanel *capture_ = nullptr;
    DataPanel *data_ = nullptr;
    DiagnosticsPanel *diagnostics_ = nullptr;
    SettingsDialog *settings_ = nullptr;

    EStopButton *estop_ = nullptr;
    AlertFrame *alert_ = nullptr;
    ToastHost *toasts_ = nullptr;
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

    bool didInitialFit_ = false;

    /// Either the built-in simulator or the real bridge client. The window
    /// deliberately does not know which: everything goes through the interface.
    gcs::robot::RobotLink *robot_ = nullptr;
    gcs::sim::MapData mapData_;
};

}  // namespace gcs::ui
