// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#pragma once

// The control station's view of the robot.
//
// The production implementation is BridgeClient, the raw TCP connection to a
// robot endpoint (docs/bridge_protocol.md). A physical robot and MuJoCo
// simulator expose the same endpoint, so MainWindow has no simulator branch.
//
// Commands are fire-and-forget from the caller's point of view. Rejections
// come back as robotEvent() with a catalog code, because a rejected command is
// something the operator needs to see in the log, not a return value the UI
// would have to find a place to display.

#include <QObject>
#include <QVariantMap>

#include <optional>

#include "mapview/MapInfo.h"

#include "robot/RobotTypes.h"

namespace hmi::robot {

/// An occupancy grid and its metadata, as the map arrives from the robot.
struct MapData {
    hmi::map::MapInfo info;
    QList<qint8> grid;   ///< -1 unknown, 0 free, 100 occupied
};

class RobotLink : public QObject {
    Q_OBJECT
public:
    explicit RobotLink(QObject *parent = nullptr) : QObject(parent) {}
    ~RobotLink() override = default;

    // ---- motion ---------------------------------------------------------

    /// Manual jog. Must be published continuously while a control is held:
    /// the bridge latches zero if it stops arriving, so a frozen control
    /// station cannot leave the robot driving.
    virtual void setCmdVel(double vx, double vy, double wz) = 0;

    virtual void requestGoal(double x, double y, double theta) = 0;
    virtual void setInitialPose(double x, double y, double theta)
    { Q_UNUSED(x); Q_UNUSED(y); Q_UNUSED(theta); }
    virtual void cancelNav() = 0;
    virtual void pauseNav() {}
    virtual void resumeNav() {}
    /// Robot-owned manual/autonomous speed limits in m/s and rad/s.
    virtual void setNavigationSpeedLimits(double linear, double angular)
    { Q_UNUSED(linear); Q_UNUSED(angular); }
    virtual void setNavigationSpeedSettings(double linear, double minimum, double maximum,
                                           double angular, double angularMinimum, double angularMaximum)
    { Q_UNUSED(linear); Q_UNUSED(minimum); Q_UNUSED(maximum);
      Q_UNUSED(angular); Q_UNUSED(angularMinimum); Q_UNUSED(angularMaximum); }
    virtual void setNavigationSpeedRanges(double minimum, double maximum,
                                         double angularMinimum, double angularMaximum)
    { Q_UNUSED(minimum); Q_UNUSED(maximum); Q_UNUSED(angularMinimum); Q_UNUSED(angularMaximum); }

    // ---- mission --------------------------------------------------------
    virtual void setWaypoints(const QList<QVariantMap> &waypoints) = 0;
    virtual void setWaypoints(const QList<QVariantMap> &waypoints,
                              const QList<QVariantMap> &expectedPoints, const QString &mapId)
    { Q_UNUSED(expectedPoints); Q_UNUSED(mapId); setWaypoints(waypoints); }

    /// Replaces the fixed locations: the charging station and the start point.
    ///
    /// These have to reach the robot, not just the screen. The robot drives to
    /// the dock on its own - when a run finishes, and when the battery gets
    /// low - so a dock the operator re-taught here and nowhere else means the
    /// place shown and the place driven to are different places.
    virtual void setLocations(const QList<QVariantMap> &locations) = 0;

    /// Replaces the surveyed AprilTag list. Tags are physical objects on the
    /// wall, so this is a record of a survey, not a command to move anything -
    /// but the robot needs it to know which tag it is looking at.
    virtual void setMarkers(const QList<QVariantMap> &markers) = 0;
    virtual void setMarkers(const QList<QVariantMap> &markers,
                            const QList<QVariantMap> &expectedMarkers, const QString &mapId)
    { Q_UNUSED(expectedMarkers); Q_UNUSED(mapId); setMarkers(markers); }

    /// Takes a photograph. The robot decides whether it may - it refuses while
    /// moving (statement of work 2.2.4) - and saves the original itself; only
    /// a preview comes back over the link.
    virtual void triggerCapture(const QVariantMap &metadata) { Q_UNUSED(metadata); }

    /// Battery policy, in percent.
    ///
    /// returnAt: below this the robot abandons the run and drives to the dock.
    /// departAt: it will not leave the dock until charged to this.
    ///
    /// The control station only sets these; the robot enforces them. If the
    /// station held the rule, a crashed or disconnected station during a run
    /// would leave the robot driving until flat, possibly under a train where
    /// recovering it means moving the train. Same reasoning as the safety gate
    /// in protocol section 4.
    virtual void setBatteryPolicy(double returnAt, double departAt) = 0;
    virtual QList<QVariantMap> waypoints() const = 0;

    virtual void missionStart() = 0;
    virtual void missionPause() = 0;

    /// Resume is always explicit. Nothing in the system may resume autonomous
    /// driving on its own after a stop.
    virtual void missionResume() = 0;
    virtual void missionStop() = 0;
    virtual void returnToDock() {}
    virtual MissionState missionState() const = 0;

    /// The robot owns mission definitions; the HMI requests and edits a view.
    virtual void requestMissions() {}
    virtual void saveMission(const QVariantMap &mission, quint64 expectedRevision)
    { Q_UNUSED(mission); Q_UNUSED(expectedRevision); }
    virtual void archiveMission(const QString &id, quint64 expectedRevision)
    { Q_UNUSED(id); Q_UNUSED(expectedRevision); }
    virtual void startMission(const QString &id) { Q_UNUSED(id); }

    // ---- safety ---------------------------------------------------------

    /// Engaging is never gated on anything: it must work on the first click.
    virtual void engageEstop() = 0;
    virtual void releaseEstop() = 0;
    virtual bool estopEngaged() const = 0;

    virtual void setMode(DriveMode mode) = 0;
    virtual DriveMode mode() const = 0;
    virtual bool modeChangePending() const { return false; }

    // ---- arm ------------------------------------------------------------
    // ---- Base posture -------------------------------------------------------
    //
    // The robot decides what is safe. The UI only greys buttons out; the
    // ruling is the bridge's, and it refuses while the arm is moving or the
    // base is driving.
    //
    // damp releases the joints, so pressing it while the robot stands drops
    // it where it is. That is why confirmation is carried separately.
    virtual void setBasePosture(const QString &posture, bool confirm = false)
    {
        Q_UNUSED(posture);
        Q_UNUSED(confirm);
    }

    /// The last posture the robot reached. Empty when unknown.
    virtual QString basePosture() const { return {}; }

    /// Which of the base and the arm currently holds permission to move.
    virtual QString motionAuthority() const { return {}; }

    virtual void setArmJointGoal(const QList<double> &q) = 0;
    virtual void setArmEeGoal(const QVariantMap &pose) { Q_UNUSED(pose); }
    virtual void setArmPreset(const QString &name) = 0;
    virtual void stopArm() = 0;
    virtual void requestArmPosePresets() {}
    virtual void saveArmPosePreset(const QVariantMap &preset) { Q_UNUSED(preset); }
    virtual void updateArmPosePreset(const QVariantMap &preset, quint64 expectedRevision)
    { Q_UNUSED(preset); Q_UNUSED(expectedRevision); }
    virtual void archiveArmPosePreset(const QString &id, quint64 expectedRevision)
    { Q_UNUSED(id); Q_UNUSED(expectedRevision); }

    // ---- link -----------------------------------------------------------
    virtual bool isConnected() const = 0;

    /// Short description of the selected endpoint, shown in the title bar.
    /// Localised strings live in the implementation so that this header stays
    /// a plain English API reference.
    virtual QString describe() const = 0;

    /// A map the link can supply before any arrives over the wire.
    ///
    /// The bridge returns nothing: the robot sends the real map on
    /// map/occupancy and the screen waits until it arrives.
    virtual std::optional<MapData> initialMap() const { return std::nullopt; }

    /// Markers the link knows about up front. Empty for the bridge.
    virtual QList<QVariantMap> markers() const { return {}; }

    /// Fixed poses the link knows about: the charging station the robot
    /// returns to on its own, and the start point the operator sends it to.
    ///
    /// Both come from the robot over state/locations, or from the operator
    /// teaching them - in which case they go back to the robot and come round
    /// again, which is what makes them survive a restart of this program.
    virtual QVariantMap dockPose() const { return {}; }
    virtual QVariantMap homePose() const { return {}; }

    /// Starts a link that needs explicit activation. BridgeClient connects when
    /// the operator selects a configured robot, so it does nothing here.
    virtual void start() {}

signals:
    /// A complete snapshot. Emitted at a steady rate rather than on every
    /// arriving field, so panels never render a half-updated picture.
    void telemetry(const hmi::robot::Telemetry &tm);

    /// Something the operator should see in the log, identified by a catalog
    /// code so the UI does not have to invent wording.
    void robotEvent(const QString &code, const QVariantMap &detail);

    /// The mission state is owned by the robot side. The UI follows it rather
    /// than tracking its own copy, so the buttons cannot disagree with reality.
    void missionStateChanged(hmi::robot::MissionState state);
    void missionStatusReported(hmi::robot::MissionState state,
                               const QString &reason, const QString &detail);
    /// Progress belongs to the robot's active plan, not to the waypoint catalog.
    void missionProgressChanged(const QString &missionId, int index, int total);
    void missionsChanged(const QList<QVariantMap> &missions);
    /// The robot's committed waypoint catalog, also sent after a map switch.
    void waypointsChanged(const QList<QVariantMap> &waypoints);
    void fixedLocationsChanged();
    void mapMarkersChanged();
    void armPosePresetsChanged(const QList<QVariantMap> &presets);
    void commandResult(const QString &channel, bool ok, const QString &code,
                       const QString &message);

    /// The base posture or the motion authority changed.
    void baseStateChanged(const QString &posture, const QString &authority);

    void connectionChanged(bool connected);
    void navigationSpeedLimitsChanged(double linear, double minimum, double maximum,
                                       double angular, double angularMinimum,
                                       double angularMaximum, bool autonomousApplied);

    /// Drive mode confirmed by the robot's safety state, not merely requested
    /// by the operator. A new connection must report its mode again.
    void driveModeReported(hmi::robot::DriveMode mode);

    /// Which robot is on the other end, once it has said so.
    ///
    /// Separate from telemetry because it is not a measurement: it changes only
    /// when the connection does, and the screen has to show it even when no
    /// telemetry is arriving. An operator who cannot see which machine they are
    /// driving will eventually drive the wrong one.
    void robotIdentity(const QString &id, const QString &name);
};

}  // namespace hmi::robot
