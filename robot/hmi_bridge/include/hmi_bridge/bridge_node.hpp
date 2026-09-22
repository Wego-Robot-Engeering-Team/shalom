// Copyright (c) 2026 WeGo Robotics. All rights reserved.

#pragma once

// Bridge between the control station and the ROS 2 stack.
//
// This node is the boundary of the DDS domain. The control station does not
// run ROS 2: it connects over a single raw TCP socket, and everything it sees
// or commands passes through here. See docs/bridge_protocol.md.
//
// SAFETY IS NOT THIS NODE'S JOB
// -----------------------------
// The one-second communication-loss stop and the one-second emergency stop
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
#include <nav2_msgs/srv/load_map.hpp>
#include <nav_msgs/msg/occupancy_grid.hpp>
#include <nav_msgs/msg/path.hpp>
#include <nav_msgs/msg/odometry.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp/parameter_client.hpp>
#include <rclcpp_action/rclcpp_action.hpp>
#include <sensor_msgs/msg/battery_state.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <sensor_msgs/msg/compressed_image.hpp>
#include <sensor_msgs/msg/joint_state.hpp>
#include <unordered_map>

#include <std_srvs/srv/trigger.hpp>
#include <shalom_interfaces/msg/mission_plan.hpp>
#include <shalom_interfaces/msg/mission_state.hpp>
#include <shalom_interfaces/msg/motion_authority.hpp>
#include <shalom_interfaces/msg/safety_heartbeat.hpp>
#include <shalom_interfaces/msg/safety_state.hpp>
#include <shalom_interfaces/srv/authority_request.hpp>
#include <shalom_interfaces/srv/configure_mission.hpp>
#include <shalom_interfaces/srv/mission_control.hpp>
#include <shalom_interfaces/srv/safety_command.hpp>
#include <tf2_ros/buffer.h>
#include <tf2_ros/transform_listener.h>

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "hmi_bridge/envelope.hpp"
#include "hmi_bridge/tcp_server.hpp"

namespace hmi_bridge {

/// TCP/HMI adapter. Mission, safety, and authority decisions belong to their
/// independent ROS processes and cross this boundary only through typed APIs.
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

    /// Publishes the control station's liveness for the safety node. Velocity
    /// leases are owned by teleop_bridge's dedicated UDP ingress.
    void tickSafety();
    /// Publishes robot-side safety state even if localization is unavailable.
    void publishSafety();
    void requestBaseAuthority();
    void requestSafetyResume();
    void sendSafetyCommand(const Envelope &request, uint8_t operation);
    [[nodiscard]] bool estopActive() const;

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
    /// So the list has to *be* the robot's sensors: LiDAR, IMU, D455 and the
    /// two joint streams. An earlier version reported whatever this node
    /// happened to subscribe to, which put "pose" and "map" on a screen headed
    /// "센서 상태" - those are things the robot computes, not things it senses,
    /// and their absence means something completely different.
    ///
    /// Which sensors to watch comes from bridge.yaml rather than being compiled
    /// in: the arm and D455 arrive on their own schedule, and a robot
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

    // ---- 본체 자세 ----------------------------------------------------------
    //
    // B2 는 SDK 의 SportClient 를 std_srvs/Trigger 로 노출한다. 브릿지는 그
    // 서비스를 부르기만 하고 무엇이 안전한지는 로봇이 정한다.
    //
    // 자세 전환은 팔이 접혀 있어야 안전하다. 팔이 펴진 채 앉으면 차체나 바닥에
    // 부딪힌다. 그래서 모션 권한이 팔에 있는 동안은 거절한다 — 화면이 버튼을
    // 잠그는 것과 별개로 판정은 여기서 한다.
    void handleBasePosture(const Envelope &request);

    /// 선행 조건 미충족으로 자세 전환을 거절한다. 응답과 함께
    /// BASE_POSTURE_BLOCKED 를 이력에 남긴다.
    void refusePosture(const Envelope &request, const std::string &code,
                       const std::string &message);

    /// 이 자세 이름이 지원되는지. 아니면 nullptr.
    static const char *postureService(const std::string &posture);

    /// 본체 자세와 현재 모션 권한을 관제로 올린다.
    void publishBase();

    std::unordered_map<std::string, rclcpp::Client<std_srvs::srv::Trigger>::SharedPtr>
        postureClients_;
    std::string motionAuthority_{"none"};
    std::string basePosture_{"unknown"};

    /// 본체 드라이버 없이 자세 명령을 로그로만 처리한다.
    ///
    /// 시뮬레이터에는 SportClient 서비스가 없어 실제 자세 전환을 할 수 없다.
    /// 그렇다고 요청이 통째로 막히면 관제 화면과 연동 코드를 시뮬레이터에서
    /// 시험할 수 없다. 이 값이 켜지면 요청을 받아 로그에 남기고 상태만
    /// 갱신한다.
    ///
    /// 기본값은 꺼짐이고 simulation_bringup 의 설정에서만 켠다. 실기에서 켜지면
    /// 화면은 앉았다고 표시하는데 로봇은 서 있는 상태가 되므로, 그 사실이
    /// 로그에 매번 드러나도록 경고로 찍는다.
    bool postureDryRun_ = false;

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
    void publishMapCatalog();
    void publishActiveMap();
    bool loadMapBundle(const std::string &map_id, std::string *error = nullptr);
    bool saveMapState(const char *filename, const json &state);

    // ---- mission adapter ------------------------------------------------
    /// Converts the robot-owned map bundle into an immutable typed plan.
    std::optional<shalom_interfaces::msg::MissionPlan> makeMissionPlan(
        std::string *error = nullptr);
    void configureAndStartMission(const Envelope &request);
    void sendMissionControl(const Envelope &request, uint8_t operation);
    void pauseMissionForManualTakeover();
    void onMissionState(const shalom_interfaces::msg::MissionState::SharedPtr message);
    /// 등록된 위치에서 충전 스테이션을 찾는다. 없으면 -1.
    int findDock() const;
    void publishMission();

    // ---- 촬영 -----------------------------------------------------------
    //
    // 저장은 로봇이 한다. 원본이 관제를 거치지 않는 것과 같은 이유다 —
    // 링크가 끊겨도 사진은 남아야 하고, 관제 PC 가 저장 경로를 알 이유도 없다.
    void handleCapture(const Envelope &request);

    /// 지금 움직이고 있는지. 과업지시서 2.2.4 가 정지 상태 촬영을 요구한다.
    bool isMoving() const;

    /// 깊이 이미지 한가운데의 거리(mm). 못 읽으면 음수.
    double centreDistanceMm() const;

    void publishCaptureSpool();

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

    /// 촬영 원본을 무손실 PNG 로 만든다. rgb8·bgr8·mono8 을 받는다.
    ///
    /// 카메라가 압축 영상을 내지 않는다. realsense2_camera 는 컬러를 원본
    /// Image 로만 발행하고 image_transport 압축 발행자를 만들지 않으므로,
    /// CompressedImage 를 기다리면 촬영이 영원히 "카메라 영상이 없습니다" 로
    /// 거절된다 — 실제로 그 상태였다.
    ///
    /// 그래서 원본을 받아 저장 시점에 한 번만 누른다. 카메라가 누른 것을 다시
    /// 누르는 것이 아니라 한 번만 누르는 셈이라 화질이 오히려 낫고, 무손실을
    /// 고른 것은 이 파일이 점검 근거로 남기 때문이다(과업지시서 2.2.4).
    /// zlib 은 지도 PNG 때문에 이미 링크돼 있어 의존성이 늘지 않는다.
    static std::string encodeImagePng(const sensor_msgs::msg::Image &image);

    // ---- parameters ------------------------------------------------------
    int port_ = 9090;
    std::string mapFrame_ = "map";
    std::string baseFrame_ = "base_link";

    /// The control station is considered present while its heartbeat is no
    /// older than this. The safety node independently enforces the same one-second contract
    /// and deliberately longer.
    std::chrono::milliseconds heartbeatTimeout_{1000};

    // ---- state -----------------------------------------------------------
    TcpServer server_;
    bool estopEngaged_ = false;
    bool softwareEstopRequested_ = false;
    std::string safetyState_{"unknown"};
    bool manualMode_ = false;
    rclcpp::Time lastHeartbeat_;
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
    std::string mapsDir_ = "/var/lib/shalom/maps";

    json waypoints_ = json::array();
    json markers_ = json::array();
    bool wasConnected_ = false;

    shalom_interfaces::msg::MissionState missionState_;
    bool haveMissionState_ = false;
    uint64_t missionPlanRevision_ = 0;
    uint64_t rosRequestSequence_ = 0;

    // 로봇 식별자. 지금은 한 대뿐이라 화면에 이름을 띄우는 데만 쓰지만,
    // 여러 대가 되면 관제가 어느 로봇의 값인지 가르는 근거가 된다. 나중에
    // 넣으려면 프로토콜을 고쳐야 하므로 지금 자리를 만들어 둔다.
    std::string robotId_ = "R1";
    std::string robotName_ = "1호기";

    // 촬영. 압축 이미지를 그대로 받아 그대로 쓴다 — 인코더를 따로 두면
    // 같은 그림을 두 번 누르는 셈이고, compressed_image_transport 가 이미
    // 카메라 노드 쪽에서 해 준다.
    rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr colorSub_;
    rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr depthSub_;
    // 속도는 TF 자세 변화로 잰다.
    //
    // kiss_icp 는 자세만 내고 twist 는 0 으로 둔다. 시뮬의 /b2/odom_gt 에는
    // 속도가 있지만 실기에는 없다. 어느 오도메트리를 쓰든 map->base_link 는
    // 있으므로, 그것을 미분하는 편이 양쪽에서 같게 동작한다.
    double lastPoseX_ = 0.0;
    double lastPoseY_ = 0.0;
    double lastPoseTheta_ = 0.0;
    rclcpp::Time lastPoseAt_;
    sensor_msgs::msg::Image::ConstSharedPtr lastColor_;
    sensor_msgs::msg::Image::ConstSharedPtr lastDepth_;
    double speedLinear_ = 0.0;
    double speedAngular_ = 0.0;
    rclcpp::Time lastOdomAt_;

    std::string spoolDir_;
    bool captureEnabled_ = false;
    double maxCaptureLinear_ = 0.03;    ///< m/s
    double maxCaptureAngular_ = 0.05;   ///< rad/s
    int capturesTaken_ = 0;

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
    rclcpp::Publisher<shalom_interfaces::msg::SafetyHeartbeat>::SharedPtr linkAlivePub_;
    rclcpp::Publisher<sensor_msgs::msg::JointState>::SharedPtr armCmdPub_;
    /// Manual-mode hold for the base. Zero velocity at 20 Hz, which is what
    /// keeps twist_mux from falling through to Nav2 while the operator has
    /// taken manual control. The arm has the same source in joint_mux.
    rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr baseHoldPub_;

    rclcpp_action::Client<NavigateToPose>::SharedPtr navClient_;
    rclcpp::Client<nav2_msgs::srv::LoadMap>::SharedPtr mapLoadClient_;
    rclcpp::Client<shalom_interfaces::srv::ConfigureMission>::SharedPtr missionConfigureClient_;
    rclcpp::Client<shalom_interfaces::srv::MissionControl>::SharedPtr missionControlClient_;
    rclcpp::Client<shalom_interfaces::srv::SafetyCommand>::SharedPtr safetyCommandClient_;
    rclcpp::Client<shalom_interfaces::srv::AuthorityRequest>::SharedPtr authorityRequestClient_;

    rclcpp::Subscription<sensor_msgs::msg::BatteryState>::SharedPtr batterySub_;
    rclcpp::Subscription<sensor_msgs::msg::JointState>::SharedPtr jointSub_;
    rclcpp::Subscription<nav_msgs::msg::Path>::SharedPtr planSub_;
    rclcpp::Subscription<nav_msgs::msg::OccupancyGrid>::SharedPtr mapSub_;
    rclcpp::Subscription<shalom_interfaces::msg::SafetyState>::SharedPtr safetyStateSub_;
    rclcpp::Subscription<shalom_interfaces::msg::MotionAuthority>::SharedPtr authoritySub_;
    rclcpp::Subscription<shalom_interfaces::msg::MissionState>::SharedPtr missionStateSub_;

    std::unique_ptr<tf2_ros::Buffer> tfBuffer_;
    std::shared_ptr<tf2_ros::TransformListener> tfListener_;

    /// 관제 포트를 연다. 실패하면 false 를 돌려주고 재시도 타이머가 계속 부른다.
    bool openControlPort();

    rclcpp::TimerBase::SharedPtr bindRetryTimer_;
    bool bindFailed_ = false;

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

}  // namespace hmi_bridge
