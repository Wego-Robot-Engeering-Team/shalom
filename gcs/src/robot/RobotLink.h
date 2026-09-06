#pragma once

// The control station's view of the robot.
//
// Two implementations exist:
//
//   - SimRobot:     an in-process simulator, used until the bridge is available
//                   and afterwards for exercising the interface offline
//   - BridgeClient: the real connection over raw TCP (docs/bridge_protocol.md)
//
// The command surface mirrors the protocol's command channels one for one, so
// that swapping implementations is a matter of what MainWindow constructs. The
// simulator's test suite is written against this interface, which makes it the
// acceptance criteria the bridge client has to meet rather than a throwaway.
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

namespace gcs::robot {

/// An occupancy grid and its metadata, as the map arrives from the robot.
struct MapData {
    gcs::map::MapInfo info;
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
    virtual void cancelNav() = 0;

    // ---- mission --------------------------------------------------------
    virtual void setWaypoints(const QList<QVariantMap> &waypoints) = 0;

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
    virtual MissionState missionState() const = 0;

    // ---- safety ---------------------------------------------------------

    /// Engaging is never gated on anything: it must work on the first click.
    virtual void engageEstop() = 0;
    virtual void releaseEstop() = 0;
    virtual bool estopEngaged() const = 0;

    virtual void setMode(DriveMode mode) = 0;
    virtual DriveMode mode() const = 0;

    // ---- arm ------------------------------------------------------------
    virtual void setArmJointGoal(const QList<double> &q) = 0;
    virtual void setArmPreset(const QString &name) = 0;
    virtual void stopArm() = 0;

    // ---- link -----------------------------------------------------------
    virtual bool isConnected() const = 0;

    /// Short description of what is on the other end, shown in the title bar
    /// badge: the word "simulator" in the operator's language, or the bridge
    /// address and port. Localised strings live in the implementation so that
    /// this header stays a plain English API reference.
    virtual QString describe() const = 0;

    /// A map the link can supply before any arrives over the wire.
    ///
    /// The bridge returns nothing: the real map comes on map/occupancy and the
    /// screen says it is waiting. The testbed returns a stand-in so the screen
    /// is usable without a robot. MainWindow does not need to know which it is
    /// talking to - it used to test for the simulator type by hand, which put
    /// the simulator into the delivered binary.
    virtual std::optional<MapData> initialMap() const { return std::nullopt; }

    /// Markers the link knows about up front. Empty for the bridge.
    virtual QList<QVariantMap> markers() const { return {}; }

    /// Charging station pose the link knows about up front. Empty for the
    /// bridge - it comes from the robot, or the operator teaches it.
    virtual QVariantMap dockPose() const { return {}; }

    /// Starts producing telemetry. The bridge connects on construction and
    /// does nothing here; the testbed starts its clock.
    ///
    /// The window used to reach for the simulator type to call start(), which
    /// is how the simulator ended up compiled into every build.
    virtual void start() {}

signals:
    /// A complete snapshot. Emitted at a steady rate rather than on every
    /// arriving field, so panels never render a half-updated picture.
    void telemetry(const gcs::robot::Telemetry &tm);

    /// Something the operator should see in the log, identified by a catalog
    /// code so the UI does not have to invent wording.
    void robotEvent(const QString &code, const QVariantMap &detail);

    /// The mission state is owned by the robot side. The UI follows it rather
    /// than tracking its own copy, so the buttons cannot disagree with reality.
    void missionStateChanged(gcs::robot::MissionState state);

    void connectionChanged(bool connected);
};

}  // namespace gcs::robot
