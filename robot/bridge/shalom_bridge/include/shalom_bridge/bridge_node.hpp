#pragma once

// Bridge between the control station and the ROS 2 stack.
//
// This node is the boundary of the DDS domain. The control station does not
// run ROS 2: it connects over a single raw TCP socket, and everything it sees
// or commands passes through here. See docs/bridge_protocol.md.
//
// SAFETY IS NOT THIS NODE'S JOB
// -----------------------------
// The three-second communication-loss stop and the one-second emergency stop
// are enforced by a separate safety node. This one only reports liveness: it
// publishes a heartbeat topic while the control station's heartbeat is fresh,
// and stops publishing otherwise.
//
// The separation matters because *this node crashing must also stop the
// robot*. If the stop decision lived here, a segfault would leave the robot
// driving with nobody watching. Publishing liveness and letting another
// process act on its absence makes the failure safe by construction.
//
// THREADING
// ---------
// The TCP server runs its own thread and hands frames over through a queue.
// Everything in this class runs on the executor thread, so no ROS 2 handle is
// ever touched from the network thread.

#include <geometry_msgs/msg/twist.hpp>
#include <sensor_msgs/msg/joint_state.hpp>
#include <nav2_msgs/action/navigate_to_pose.hpp>
#include <nav_msgs/msg/occupancy_grid.hpp>
#include <nav_msgs/msg/path.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp/parameter_client.hpp>
#include <rclcpp_action/rclcpp_action.hpp>
#include <sensor_msgs/msg/battery_state.hpp>
#include <sensor_msgs/msg/joint_state.hpp>
#include <std_msgs/msg/bool.hpp>
#include <tf2_ros/buffer.h>
#include <tf2_ros/transform_listener.h>

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "shalom_bridge/envelope.hpp"
#include "shalom_bridge/tcp_server.hpp"

namespace shalom_bridge {

class BridgeNode : public rclcpp::Node {
public:
    BridgeNode();
    ~BridgeNode() override;

private:
    // ---- link ------------------------------------------------------------
    void pollLink();
    void handleFrame(const inspection::Frame &frame);
    void handleRequest(const Envelope &request);
    void handleHeartbeat(const Envelope &heartbeat);

    void sendEnvelope(const Envelope &env, bool lossy = false,
                      const std::string &payload = {});
    void respond(const Envelope &request, bool ok, const std::string &code = {},
                 const std::string &message = {});

    /// Publishes the control station's liveness for the safety node, and
    /// latches the jog command to zero when it stops arriving.
    void tickSafety();

    // ---- telemetry -------------------------------------------------------
    void publishPose();
    void publishHealth();
    void publishNav();

    /// CPU, memory and temperature of the machine this node runs on.
    ///
    /// The station displays these so an operator can tell a robot that is
    /// thinking hard from one that has stopped responding, and so a thermal
    /// problem in an enclosed robot is visible before it throttles. They are
    /// read here rather than sent from the station because they describe the
    /// *robot's* computer - protocol section 9 puts every measurement on the
    /// side that can actually make it.
    void publishSystem();

    /// One sensor the robot is expected to have, and how it is doing.
    ///
    /// The station shows this list and nothing else - it has no idea what
    /// sensors exist, and protocol section 9.2 puts the judgement on this side.
    /// So the list has to *be* the robot's sensors: LiDAR, IMU, the cameras,
    /// the two joint streams. An earlier version reported whatever this node
    /// happened to subscribe to, which put "pose" and "map" on a screen headed
    /// "센서 상태" - those are things the robot computes, not things it senses,
    /// and their absence means something completely different.
    ///
    /// Which sensors to watch comes from bridge.yaml rather than being compiled
    /// in: the arm and the cameras arrive on their own schedule, and a robot
    /// without them yet should say so rather than have the line quietly missing.
    struct Sensor {
        std::string id;        ///< stable key the station matches on
        std::string name;      ///< what the operator reads
        std::string topic;
        std::string type;      ///< ROS type name, for the generic subscription
        double expectedHz = 0.0;

        rclcpp::GenericSubscription::SharedPtr sub;
        rclcpp::Time lastSeen;
        bool everSeen = false;
        /// Smoothed arrival rate. A single interval is far too noisy to show.
        double measuredHz = 0.0;
    };

    /// Marks a sensor as heard from now and folds the interval into its rate.
    void markSeen(Sensor &sensor);

    // ---- commands --------------------------------------------------------
    bool commandsAllowed(const Envelope &request);
    void applyCmdVel(const json &payload);

    // ---- autonomous navigation -------------------------------------------
    //
    // The station clicks a point on the map; Nav2 owns everything after that.
    // This node only forwards the goal and reports back what the action server
    // says, because a second opinion on whether a goal is reachable is one the
    // operator has no way to adjudicate.
    void startNavigation(const Envelope &request);

    /// Asks Nav2 to abandon the current goal. Safe to call when there is none.
    ///
    /// This is *not* how the robot is stopped in an emergency - the safety node
    /// does that without consulting anything here. Cancelling only keeps Nav2
    /// from resuming once the operator has taken manual control.
    void cancelNavigation(const char *reason);

    void settleGoto(bool ok, const std::string &code = {}, const std::string &message = {});

    // ---- arm ---------------------------------------------------------------
    //
    // The station commands a *posture* and the robot side generates the
    // trajectory. Feeding raw joint targets straight through would run the arm
    // into its own velocity and jerk limits, and the controller would fault
    // rather than move (see the station's ArmPanel for the same reasoning from
    // the other end).
    bool applyArmGoal(const Envelope &request, const std::vector<double> &positions);

    /// Named postures, kept here so the robot decides what "stow" means.
    ///
    /// The station has the same table for drawing its 3D preview, but the
    /// authority has to be on this side: an operator pressing "stow" is asking
    /// the robot to fold its arm, not asking it to go to whatever six numbers
    /// the station happened to be built with.
    static const std::vector<double> *armPreset(const std::string &name);

    // ---- operator-owned lists ----------------------------------------------
    //
    // Waypoints, dock and home, and the battery policy are set by the operator
    // and belong to the robot: it has to keep driving to them when the station
    // disconnects. The bridge holds them and echoes them back on their state
    // channels so a station that reconnects, or a second one that replaces the
    // first, sees what the robot is actually working from rather than its own
    // last edit.
    void publishWaypoints();
    void publishLocations();
    void publishMarkers();

    // ---- 점검 순회 -----------------------------------------------------
    //
    // 관제는 점검포인트를 순서대로 도는 하나의 작업으로 다룬다. 여기가
    // 없으면 화면의 시작·일시정지·재개·취소가 눌리는 곳이 없고, 상태를
    // 알려주지 않으니 버튼 글자도 영영 "자율주행 시작" 에 머문다.
    void startMission();
    void pauseMission(const char *why);
    void resumeMission();
    void stopMission(const char *why);

    /// 목표 하나가 끝났을 때 다음으로 넘긴다.
    void onMissionGoalFinished(bool succeeded, bool canceled);

    /// index 번째 점검포인트로 보낸다. 보낼 수 없으면 false.
    bool navigateToWaypoint(std::size_t index);
    void setWaypointStatus(std::size_t index, const char *status);
    void publishMission();

    const char *missionStateName() const;

    /// Accumulated driven path, in map coordinates.
    void publishTrail();

    /// The occupancy grid, as an 8-bit greyscale PNG.
    ///
    /// The station renders whatever image arrives, so this uses the map_server
    /// convention - 254 free, 0 occupied, 205 unknown - and a map streamed from
    /// the robot then looks the same as one loaded from a saved .pgm. Rows are
    /// flipped: an OccupancyGrid starts at the origin and counts up in y, an
    /// image starts at the top.
    ///
    /// Encoded here rather than sent as raw cells because the grid is 238 x 178
    /// and growing, and it is republished every time SLAM extends it; PNG takes
    /// that from tens of kilobytes to a few.
    static std::string encodeGridPng(const nav_msgs::msg::OccupancyGrid &grid);

    // ---- parameters ------------------------------------------------------
    int port_ = 9090;
    std::string mapFrame_ = "map";
    std::string baseFrame_ = "base_link";

    /// Jog commands stop being honoured this long after the last one arrives.
    /// Matches the value published in the protocol, and both must change
    /// together.
    std::chrono::milliseconds deadman_{300};

    /// The control station is considered present while its heartbeat is no
    /// older than this. The safety node's own three-second rule is separate
    /// and deliberately longer.
    std::chrono::milliseconds heartbeatTimeout_{1000};

    /// Ceiling on a jog command, from the statement of work.
    ///
    /// The station's sliders already stop at these figures, but a limit that
    /// exists only on the operator's screen is not a limit: a UI bug, a
    /// hand-written client during commissioning, or a replayed frame all reach
    /// `/cmd_vel` unchecked. Protocol section 4 puts admissibility on the robot
    /// side, and this is the robot side. Out-of-range values are clamped, not
    /// rejected - refusing the frame would leave the robot coasting on the last
    /// good command, which is worse than moving slower than asked.
    double maxLinVelX_ = 0.60;   ///< m/s,   RobotDef.h kVxMax
    double maxLinVelY_ = 0.40;   ///< m/s,   RobotDef.h kVyMax
    double maxAngVelZ_ = 0.80;   ///< rad/s, RobotDef.h kWzMax

    // ---- state -----------------------------------------------------------
    TcpServer server_;
    bool estopEngaged_ = false;
    bool manualMode_ = false;
    rclcpp::Time lastHeartbeat_;
    rclcpp::Time lastCmdVel_;
    geometry_msgs::msg::Twist pendingTwist_;
    bool haveJogCommand_ = false;
    std::int64_t seq_ = 0;

    // ---- navigation state ------------------------------------------------
    using NavigateToPose = nav2_msgs::action::NavigateToPose;
    using NavGoalHandle = rclcpp_action::ClientGoalHandle<NavigateToPose>;

    /// The `res` for a `cmd/goto` is held back until Nav2 has accepted or
    /// rejected the goal. Answering "ok" the moment the goal is posted would
    /// report success for a goal the planner then refuses, and the operator
    /// would be left watching a robot that never moves with nothing to read.
    std::optional<Envelope> pendingGoto_;
    rclcpp::Time pendingGotoAt_;

    std::vector<Sensor> sensors_;

    NavGoalHandle::SharedPtr navGoal_;

    /// Which goal the status below describes.
    ///
    /// A new goto preempts the running one, and Nav2 then delivers the old
    /// goal's result - CANCELED - *after* the new goal is already reporting
    /// progress. Without this check that stale result overwrites the new
    /// goal's status, and the station shows "failed" while the distance
    /// remaining ticks down. Every callback is filtered against it.
    rclcpp_action::GoalUUID navGoalId_{};
    std::string navStatus_ = "idle";
    json navGoalPoint_;               ///< the goal echoed back, or null
    double navDistance_ = 0.0;        ///< m, from Nav2 feedback
    json navEta_ = nullptr;           ///< s, or null when Nav2 does not estimate one

    /// 관제 화면에 뜨는 지도 이름. 저장된 지도와 구분되도록 이름을 붙인다.
    std::string mapId_ = "live";

    json waypoints_ = json::array();
    json markers_ = json::array();
    bool wasConnected_ = false;

    enum class Mission { Idle, Running, Paused };
    Mission mission_ = Mission::Idle;
    std::size_t missionIndex_ = 0;
    /// 지금 Nav2 에 걸린 목표가 순회의 것인지. 조작자가 지도를 눌러 보낸
    /// 목표와 구분해야, 그 목표가 끝났다고 순회가 한 칸 넘어가지 않는다.
    bool missionOwnsGoal_ = false;

    /// 영상 노드의 파라미터를 원격으로 바꾼다. 관제는 프리셋 이름만 보내고
    /// 실제 해상도·비트레이트는 영상 노드가 안다.
    std::shared_ptr<rclcpp::AsyncParametersClient> videoParams_;
    std::string videoQuality_ = "high";
    std::string videoNodeName_;

    // 마지막으로 보낸 지도. /map 은 transient_local 이라 구독 콜백이 브릿지
    // 기동 때 한 번만 뜬다. 관제가 그 뒤에 붙으면 지도를 영영 못 받으므로
    // 들고 있다가 접속할 때 다시 보낸다.
    std::string lastMapPng_;
    nlohmann::json lastMapMeta_;

    json locations_ = json::array();
    double returnAtPct_ = 25.0;   ///< battery level that sends the robot back
    double departAtPct_ = 80.0;   ///< level it will set out again at

    /// Trail points not yet sent, and whether the station should clear first.
    ///
    /// Sent as an increment rather than the whole path: the run is long and
    /// resending thousands of points twice a second would crowd out telemetry
    /// that matters. `trailReset_` is set once, so a station connecting mid-run
    /// starts from a clean line instead of appending to whatever it had.
    std::vector<std::pair<double, double>> trailPending_;
    double trailLastX_ = 0.0, trailLastY_ = 0.0;
    bool trailHasLast_ = false;
    bool trailReset_ = true;

    /// 마지막으로 보고된 팔 자세. cmd/arm/stop 이 그 자리를 목표로 되쓴다.
    std::vector<double> lastArmPositions_;

    // ---- ROS interfaces --------------------------------------------------
    rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr cmdVelPub_;
    rclcpp::Publisher<std_msgs::msg::Bool>::SharedPtr linkAlivePub_;
    rclcpp::Publisher<std_msgs::msg::Bool>::SharedPtr estopPub_;
    rclcpp::Publisher<sensor_msgs::msg::JointState>::SharedPtr armCmdPub_;

    rclcpp_action::Client<NavigateToPose>::SharedPtr navClient_;

    rclcpp::Subscription<sensor_msgs::msg::BatteryState>::SharedPtr batterySub_;
    rclcpp::Subscription<sensor_msgs::msg::JointState>::SharedPtr jointSub_;
    rclcpp::Subscription<nav_msgs::msg::Path>::SharedPtr planSub_;
    rclcpp::Subscription<nav_msgs::msg::OccupancyGrid>::SharedPtr mapSub_;

    std::unique_ptr<tf2_ros::Buffer> tfBuffer_;
    std::shared_ptr<tf2_ros::TransformListener> tfListener_;

    rclcpp::TimerBase::SharedPtr linkTimer_;
    rclcpp::TimerBase::SharedPtr poseTimer_;
    rclcpp::TimerBase::SharedPtr safetyTimer_;
    rclcpp::TimerBase::SharedPtr healthTimer_;
    rclcpp::TimerBase::SharedPtr navTimer_;
    rclcpp::TimerBase::SharedPtr systemTimer_;
    rclcpp::TimerBase::SharedPtr trailTimer_;

    /// Previous /proc/stat jiffies, for a CPU percentage over the last second.
    /// A single reading only gives the average since boot, which never moves.
    unsigned long long cpuIdle_ = 0, cpuTotal_ = 0;
};

}  // namespace shalom_bridge
