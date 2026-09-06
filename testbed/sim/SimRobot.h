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
#include <QVariantMap>

#include "mapview/MapInfo.h"
#include "robot/RobotLink.h"

class QTimer;

namespace gcs::sim {

using gcs::robot::MapData;

MapData buildMap();
QList<QVariantMap> buildWaypoints();
QList<QVariantMap> buildTags(const QList<QVariantMap> &waypoints);

using gcs::robot::DriveMode;
using gcs::robot::MissionState;
using gcs::robot::Telemetry;

/// In-process stand-in for the robot.
///
/// Implements the same interface as the real bridge client, so MainWindow does
/// not know which one it has. Its test suite is therefore also the acceptance
/// criteria for BridgeClient.
class SimRobot : public gcs::robot::RobotLink {
    Q_OBJECT
public:
    explicit SimRobot(QObject *parent = nullptr);

    /// Starts emitting telemetry at 20 Hz, matching the manual jog publish
    /// rate so a held control produces one command per simulated step.
    void start() override;
    void stop();

    // ---- commands, mirroring the protocol -------------------------------

    /// Manual jog. Ignored unless in manual mode and not stopped. Latched to
    /// zero if not refreshed within the deadman window, exactly as the bridge
    /// does (protocol section 3.1).
    void setCmdVel(double vx, double vy, double wz) override;

    void requestGoal(double x, double y, double theta) override;
    void cancelNav() override;
    void setBatteryPolicy(double returnAt, double departAt) override;

    void missionStart() override;
    void missionPause() override;
    void missionResume() override;
    void missionStop() override;

    void engageEstop() override;

    /// Release is a separate call and never happens implicitly: the statement
    /// of work forbids automatic release (2.2.5).
    void releaseEstop() override;

    void setMode(DriveMode mode) override;
    void setArmJointGoal(const QList<double> &q) override;
    void setArmPreset(const QString &name) override;
    void stopArm() override;

    // ---- state ----------------------------------------------------------
    bool estopEngaged() const override { return estop_; }
    DriveMode mode() const override { return mode_; }
    MissionState missionState() const override { return mission_; }

    QList<QVariantMap> waypoints() const override { return waypoints_; }
    void setWaypoints(const QList<QVariantMap> &waypoints) override;

    /// Always connected: there is no link to lose.
    bool isConnected() const override { return true; }

    /// Defined in the implementation because the text is localised and public
    /// headers are kept in English.
    QString describe() const override;

    /// The stand-in depot map, so the screen is usable without a robot.
    std::optional<gcs::robot::MapData> initialMap() const override;
    QList<QVariantMap> markers() const override;
    QVariantMap dockPose() const override;

    /// Advances the simulation by dt seconds and returns the new telemetry.
    /// Exposed so tests can drive it deterministically instead of waiting on
    /// a timer.
    Telemetry step(double dt);

private:
    bool driveToward(double dt, double tx, double ty, bool alignHeading, double targetTheta);
    void stepMission(double dt);
    void stepArm(double dt);
    void integrate(double dt, double vx, double vy, double wz);
    int nextPendingWaypoint() const;
    void setWaypointStatus(int index, const QString &status);

    // pose and motion
    // In front of the charging station on the depot map, which sits past the
    // end of the train in the corner of the shed.
    double x_ = -82.0, y_ = -6.0, theta_ = 0.0;
    double speed_ = 0.0;

    // Battery policy: set by the control station, enforced here.
    double returnAtPct_ = 25.0;
    double departAtPct_ = 60.0;
    bool returningForCharge_ = false;

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
    bool armMoving_ = false;

    int seqGaps_ = 0;
    int pendingUploads_ = 0;
    double uploadTimer_ = 0.0;

    QTimer *timer_ = nullptr;

    bool estop_ = false;
    DriveMode mode_ = DriveMode::Auto;
    double soc_ = 87.0;
    double t_ = 0.0;
    QList<QPointF> trail_;
};

}  // namespace gcs::sim
