#pragma once

// In-process robot simulator.
//
// This exists so the control station can be exercised end to end before the
// bridge and the real simulation are available: commands actually move the
// robot, the emergency stop actually stops it, and navigation actually drives
// to a goal.
//
// The command surface deliberately mirrors the protocol's command channels
// (docs/bridge_protocol.md section 3) one for one. Swapping this for the real
// BridgeClient should be a matter of changing what MainWindow constructs, not
// of rewriting the call sites.
//
// The physics are intentionally simple - unicycle motion, first-order joint
// tracking - because the purpose is to exercise the interface and the state
// machine, not to predict robot behaviour. Anything that depends on real
// dynamics has to be validated against the actual robot regardless.

#include <QList>
#include <QObject>
#include <QPointF>
#include <QSet>
#include <QVariantMap>

#include "mapview/MapInfo.h"
#include "panels/DiagnosticsPanel.h"

namespace gcs::sim {

/// Occupancy grid plus its metadata, as the map would arrive from the bridge.
struct MapData {
    gcs::map::MapInfo info;
    QList<qint8> grid;
};

MapData buildMap();
QList<QVariantMap> buildWaypoints();
QList<QVariantMap> buildTags(const QList<QVariantMap> &waypoints);

enum class DriveMode { Auto, Manual };
enum class MissionState { Idle, Running, Paused };

/// A single simulation frame, shaped like the telemetry the UI consumes.
struct Telemetry {
    double x = 0, y = 0, theta = 0;
    double speed = 0;              ///< m/s, magnitude
    QList<QPointF> trail;
    QList<QPointF> plan;
    double soc = 0;
    QList<double> joints;
    double manipulability = 0;
    double sigmaMin = 0;
    QSet<int> seenTags;
    double cpu = 0, mem = 0, cpuTemp = 0, gpuTemp = 0, rtt = 0;
    bool estop = false;
    QString navStatus;             ///< "idle" | "driving" | "arrived" | "blocked"

    QList<gcs::ui::SensorHealth> sensors;
    gcs::ui::LinkHealth link;
    bool nasOnline = true;
    int pendingUploads = 0;
    double spoolFreeMb = 0;
};

class SimRobot : public QObject {
    Q_OBJECT
public:
    explicit SimRobot(QObject *parent = nullptr);

    // ---- commands, mirroring the protocol -------------------------------

    /// Manual jog. Ignored unless in manual mode and not stopped. Latched to
    /// zero if not refreshed within the deadman window, exactly as the bridge
    /// does (protocol section 3.1).
    void setCmdVel(double vx, double vy, double wz);

    void requestGoal(double x, double y, double theta);
    void cancelNav();

    void missionStart();
    void missionPause();
    void missionResume();
    void missionStop();

    void engageEstop();

    /// Release is a separate call and never happens implicitly: the statement
    /// of work forbids automatic release (2.2.5).
    void releaseEstop();

    void setMode(DriveMode mode);
    void setArmJointGoal(const QList<double> &q);
    void setArmPreset(const QString &name);
    void stopArm();

    // ---- state ----------------------------------------------------------
    bool estopEngaged() const { return estop_; }
    DriveMode mode() const { return mode_; }
    MissionState missionState() const { return mission_; }

    const QList<QVariantMap> &waypoints() const { return waypoints_; }
    void setWaypoints(const QList<QVariantMap> &waypoints);

    /// Advances the simulation by dt seconds and returns the new telemetry.
    Telemetry step(double dt);

signals:
    /// Emitted for events the operator should see in the log, using catalog
    /// codes so the UI does not have to invent messages.
    ///
    /// Named robotEvent rather than event: a signal called `event` on a QObject
    /// hides the virtual QObject::event(QEvent *), which is a subtle way to
    /// break event delivery.
    void robotEvent(const QString &code, const QVariantMap &detail);
    void missionStateChanged(gcs::sim::MissionState state);

private:
    bool driveToward(double dt, double tx, double ty, bool alignHeading, double targetTheta);
    void stepMission(double dt);
    void stepArm(double dt);
    void integrate(double dt, double vx, double vy, double wz);
    int nextPendingWaypoint() const;
    void setWaypointStatus(int index, const QString &status);

    // pose and motion
    double x_ = -13.0, y_ = 0.0, theta_ = 0.0;
    double speed_ = 0.0;

    // manual jog with deadman
    double cmdVx_ = 0, cmdVy_ = 0, cmdWz_ = 0;
    double sinceCmdVel_ = 1e9;

    // navigation
    bool hasGoal_ = false;
    double goalX_ = 0, goalY_ = 0, goalTheta_ = 0;
    QString navStatus_ = QStringLiteral("idle");

    // mission
    MissionState mission_ = MissionState::Idle;
    QList<QVariantMap> waypoints_;
    int activeIndex_ = -1;
    double dwell_ = 0.0;
    int currentCar_ = -1;

    // arm
    QList<double> joints_;
    QList<double> jointTarget_;

    int seqGaps_ = 0;
    int pendingUploads_ = 0;
    double uploadTimer_ = 0.0;

    bool estop_ = false;
    DriveMode mode_ = DriveMode::Auto;
    double soc_ = 87.0;
    double t_ = 0.0;
    QList<QPointF> trail_;
};

}  // namespace gcs::sim
