// Copyright (c) 2026 WeGo Robotics. All rights reserved.

#include "hmi_bridge/bridge_node.hpp"

#include <tf2/LinearMath/Quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#include <fstream>
#include <filesystem>
#include <string>

// 점유격자를 PNG 로 눌러 관제에 보낸다 (encodeGridPng).
#include <zlib.h>

#include <tf2/LinearMath/Matrix3x3.h>
#include <tf2/LinearMath/Quaternion.h>

#include <cmath>

namespace hmi_bridge {
namespace {

// 채널 이름. 관제측 gcs/src/net/Channels.h 와 반드시 같아야 하며,
// 양쪽 모두 docs/bridge_protocol.md 를 따른다.
constexpr auto kChPose = "state/pose";
constexpr auto kChBattery = "state/battery";
constexpr auto kChSafety = "state/safety";
constexpr auto kChArm = "state/arm";
constexpr auto kChPlan = "state/plan";
constexpr auto kChMap = "map/occupancy";
constexpr auto kChHealth = "state/health";
constexpr auto kChNav = "state/nav";
constexpr auto kChSystem = "state/system";
constexpr auto kChTrail = "state/trail";
constexpr auto kChWaypoints = "state/waypoints";
constexpr auto kChLocations = "state/locations";
constexpr auto kChMarkers = "state/markers";
constexpr auto kChMission = "state/mission";
constexpr auto kChMaps = "state/maps";
constexpr auto kChActiveMap = "state/active_map";

/// Nav2 has this long to say whether it accepts a goal before the station's
/// request is answered with a failure. Goal acceptance is a planner-side
/// decision that normally lands in milliseconds; two seconds means it is wedged.
constexpr std::chrono::seconds kGoalAcceptTimeout{2};
constexpr auto kChLog = "evt/log";

constexpr auto kCmdEstop = "cmd/estop";
constexpr auto kCmdEstopRelease = "cmd/estop_release";
constexpr auto kCmdMode = "cmd/mode";
constexpr auto kCmdGoto = "cmd/goto";
constexpr auto kCmdNavCancel = "cmd/nav_cancel";
constexpr auto kCmdWaypointsSet = "cmd/waypoints/set";
constexpr auto kCmdLocationsSet = "cmd/locations/set";
constexpr auto kCmdMarkersSet = "cmd/markers/set";
constexpr auto kCmdMapsList = "cmd/maps/list";
constexpr auto kCmdMapsSelect = "cmd/maps/select";
constexpr auto kCmdMapsRename = "cmd/maps/rename";
constexpr auto kChPreview = "capture/preview";
constexpr auto kChCaptureSpool = "state/capture_spool";
constexpr auto kCmdCapture = "cmd/capture/trigger";
constexpr auto kCmdMissionStart = "cmd/mission/start";
constexpr auto kCmdMissionPause = "cmd/mission/pause";
constexpr auto kCmdMissionResume = "cmd/mission/resume";
constexpr auto kCmdMissionStop = "cmd/mission/stop";
constexpr auto kCmdPowerPolicy = "cmd/power/policy";
constexpr auto kCmdArmPreset = "cmd/arm/preset";
constexpr auto kCmdArmJointGoal = "cmd/arm/joint_goal";
constexpr auto kCmdArmEeGoal = "cmd/arm/ee_goal";
constexpr auto kCmdArmStop = "cmd/arm/stop";
constexpr auto kCmdBasePosture = "cmd/base/posture";
constexpr auto kChBase = "state/base";

/// 주행 결과를 카탈로그에 등록된 코드로 옮긴다.
///
/// 예전에는 "NAV_" + navStatus_ 로 문자열을 조립했다. 그러면 카탈로그에
/// 없는 코드가 런타임에 생기고, 관제는 그 코드를 설명하지 못한 채 원문만
/// 띄운다. 조작자에게는 원인도 조치도 없는 줄 하나가 남을 뿐이다.
const char *navResultCode(const std::string &status)
{
    if (status == "succeeded")
        return "MISSION_WAYPOINT_REACHED";
    if (status == "canceled")
        return "NAV_GOAL_CANCELED";
    if (status == "rejected")
        return "NAV_GOAL_REJECTED";
    return "NAV_GOAL_FAILED";
}

const char *safetyStateName(uint8_t state)
{
    using State = shalom_interfaces::msg::SafetyState;
    switch (state) {
    case State::INITIALIZING: return "initializing";
    case State::CONTROLLED_STOP: return "controlled_stop";
    case State::NORMAL: return "normal";
    case State::E_STOP_LATCHED: return "e_stop_latched";
    case State::FAULT: return "fault";
    default: return "unknown";
    }
}

const char *authorityName(uint8_t state)
{
    using Authority = shalom_interfaces::msg::MotionAuthority;
    switch (state) {
    case Authority::NONE: return "none";
    case Authority::BASE_ACTIVE: return "base_active";
    case Authority::BASE_STOPPING: return "base_stopping";
    case Authority::ARM_ACTIVE: return "arm_active";
    case Authority::ARM_STOPPING: return "arm_stopping";
    default: return "unknown";
    }
}

const char *missionStateName(uint8_t state)
{
    using State = shalom_interfaces::msg::MissionState;
    switch (state) {
    case State::IDLE: return "idle";
    case State::READY: return "ready";
    case State::RUNNING: return "running";
    case State::PAUSING: return "pausing";
    case State::PAUSED: return "paused";
    case State::RECOVERING: return "recovering";
    case State::RETURNING: return "returning";
    case State::COMPLETED: return "completed";
    case State::FAILED: return "failed";
    default: return "failed";
    }
}

/// FR3 joint names, in the order the arm reports them.
const std::vector<std::string> kArmJointNames{
    "fr3_shoulder", "fr3_upperarm", "fr3_forearm",
    "fr3_wrist1", "fr3_wrist2", "fr3_wrist3"};

/// 트레일을 이만큼 움직였을 때만 한 점을 남긴다. 서 있는 로봇이 초당 두 점씩
/// 같은 자리를 쌓으면 화면의 선이 뭉치고 대역폭만 먹는다.
constexpr double kTrailMinStepM = 0.05;

}  // namespace

BridgeNode::BridgeNode() : rclcpp::Node("hmi_bridge")
{
    port_ = int(declare_parameter("port", port_));
    robotId_ = declare_parameter("robot_id", robotId_);
    robotName_ = declare_parameter("robot_name", robotName_);
    // 식별자는 이제 안전 장치다. 나가는 모든 프레임에 찍히고, 이것으로
    // 들어오는 명령의 수신자를 확인한다. 비워 두면 그 확인이 통째로 꺼지므로
    // 조용히 빈 값을 받아들이지 않는다.
    if (robotId_.empty()) {
        throw std::invalid_argument("robot_id must be configured from robot metadata");
    }
    RCLCPP_INFO(get_logger(), "로봇 식별자 %s (%s)", robotId_.c_str(),
                robotName_.c_str());
    mapFrame_ = declare_parameter("map_frame", mapFrame_);
    baseFrame_ = declare_parameter("base_frame", baseFrame_);
    mapsDir_ = declare_parameter("maps_dir", mapsDir_);
    const auto initialMap = declare_parameter("initial_map", std::string{});
    if (!initialMap.empty() && initialMap != "none" && initialMap != "slam") {
        std::string mapId = initialMap;
        // navigation.launch.py는 map ID를 map_server가 쓸 절대 map.yaml 경로로
        // 바꾼다. 같은 LaunchConfiguration을 bridge에 넘겨도 지도별 상태를
        // 놓치지 않도록, 우리 maps_dir 안의 bundle 경로는 다시 ID로 바꾼다.
        const std::filesystem::path requested(initialMap);
        if (requested.is_absolute()) {
            std::error_code ec;
            const auto relative = std::filesystem::relative(
                requested, std::filesystem::path(mapsDir_), ec);
            if (!ec && relative.filename() == "map.yaml")
                mapId = relative.parent_path().filename().string();
            else
                mapId.clear();  // 외부의 평면 지도에는 bundle 상태가 없다.
        }
        if (mapId == "latest") {
            std::vector<std::string> ids;
            std::error_code ec;
            for (const auto &entry : std::filesystem::directory_iterator(mapsDir_, ec)) {
                if (!entry.is_directory()
                    || !std::filesystem::is_regular_file(entry.path() / "map.yaml"))
                    continue;
                ids.push_back(entry.path().filename().string());
            }
            std::sort(ids.begin(), ids.end());
            if (!ids.empty())
                mapId = ids.back();
            else
                mapId.clear();
        }
        std::string detail;
        if (!mapId.empty() && !loadMapBundle(mapId, &detail)) {
            RCLCPP_WARN(get_logger(), "시작 지도 %s 상태를 읽지 못했습니다: %s",
                        mapId.c_str(), detail.c_str());
        }
    }
    safetyCommandClient_ = create_client<shalom_interfaces::srv::SafetyCommand>(
        "/safety/command");
    authorityRequestClient_ = create_client<shalom_interfaces::srv::AuthorityRequest>(
        "/motion/authority/request");
    missionConfigureClient_ = create_client<shalom_interfaces::srv::ConfigureMission>(
        "/mission/configure");
    missionControlClient_ = create_client<shalom_interfaces::srv::MissionControl>(
        "/mission/control");

    batterySub_ = create_subscription<sensor_msgs::msg::BatteryState>(
        "battery_state", 10, [this](const sensor_msgs::msg::BatteryState::ConstSharedPtr &msg) {
            sendEnvelope(makePublish(kChBattery,
                                     json{{"soc", msg->percentage * 100.0},
                                          {"voltage", msg->voltage},
                                          {"current", msg->current},
                                          {"charging",
                                           msg->power_supply_status
                                               == sensor_msgs::msg::BatteryState::
                                                      POWER_SUPPLY_STATUS_CHARGING}}),
                         true);
        });

    jointSub_ = create_subscription<sensor_msgs::msg::JointState>(
        "fr3/joint_states", 10, [this](const sensor_msgs::msg::JointState::ConstSharedPtr &msg) {
            lastArmPositions_ = msg->position;
            // TODO(integration): 조작성 지수는 야코비안에서 계산해 함께 실어야 한다.
            // 관제는 표시만 하며 스스로 계산하지 않는다 (프로토콜 §4).
            sendEnvelope(makePublish(kChArm, json{{"positions", msg->position},
                                                  {"velocities", msg->velocity},
                                                  {"names", msg->name}}),
                         true);
        });

    planSub_ = create_subscription<nav_msgs::msg::Path>(
        "plan", 10, [this](const nav_msgs::msg::Path::ConstSharedPtr &msg) {
            json points = json::array();
            for (const auto &pose : msg->poses)
                points.push_back({pose.pose.position.x, pose.pose.position.y});
            sendEnvelope(makePublish(kChPlan, json{{"points", points}}), true);
        });

    // 맵은 크고 드물다. transient_local 로 걸어 관제가 늦게 붙어도 받는다.
    mapSub_ = create_subscription<nav_msgs::msg::OccupancyGrid>(
        "map", rclcpp::QoS(1).transient_local(),
        [this](const nav_msgs::msg::OccupancyGrid::ConstSharedPtr &msg) {
            const std::string png = encodeGridPng(*msg);
            if (png.empty()) {
                RCLCPP_WARN(get_logger(), "맵 %ux%u 를 PNG 로 만들지 못했다",
                            msg->info.width, msg->info.height);
                return;
            }
            lastMapMeta_ = json{{"width", msg->info.width},
                                {"height", msg->info.height},
                                {"resolution", msg->info.resolution},
                                {"origin",
                                 json{{"x", msg->info.origin.position.x},
                                      {"y", msg->info.origin.position.y},
                                      {"theta", 0.0}}},
                                {"map_id", mapId_},
                                {"encoding", "png"}};
            lastMapPng_ = png;
            sendEnvelope(makePublish(kChMap, lastMapMeta_), false, png);
            RCLCPP_INFO_THROTTLE(get_logger(), *get_clock(), 10000,
                                 "맵 %ux%u, PNG %zu 바이트", msg->info.width,
                                 msg->info.height, png.size());
        });

    tfBuffer_ = std::make_unique<tf2_ros::Buffer>(get_clock());
    tfListener_ = std::make_shared<tf2_ros::TransformListener>(*tfBuffer_);

    // 포트를 열지 못해도 죽지 않는다.
    //
    // 예전에는 여기서 예외를 던졌고, launch 의 respawn=true 가 2 초마다 노드를
    // 되살렸다. 포트를 쥔 쪽이 살아 있는 한 결과는 매번 같으므로, 복구되지
    // 않는 조건을 무한히 재시도하며 FATAL 로그만 쌓였다. respawn 은 간헐적
    // 크래시를 위한 장치이지 이런 상태를 위한 것이 아니다.
    //
    // 대신 프로세스를 유지한 채 주기적으로 다시 시도한다. 포트를 쥔 쪽이
    // 사라지면 그때 올라오고, 프로세스가 갈리지 않으니 ROS 그래프도 흔들리지
    // 않는다.
    using namespace std::chrono_literals;

    if (!openControlPort())
        bindRetryTimer_ = create_wall_timer(5s, [this] {
            if (openControlPort())
                bindRetryTimer_->cancel();
        });

    // 본체 자세는 B2 드라이버의 Trigger 서비스를 부른다. 드라이버가 없으면
    // 클라이언트는 만들어지되 호출이 실패하고, 그 사유가 관제로 간다.
    for (const auto &name : {"stand_up", "stand_down", "balance_stand",
                             "recovery_stand", "damp"})
        postureClients_[name] = create_client<std_srvs::srv::Trigger>(name);
    postureDryRun_ = declare_parameter("base.posture_dry_run", false);
    if (postureDryRun_)
        RCLCPP_WARN(get_logger(),
                    "본체 자세 명령을 로그로만 처리합니다 (base.posture_dry_run). "
                    "실기에서는 반드시 꺼야 합니다.");

    // 모션 권한을 듣는다. 팔이 움직이는 중에는 자세 전환을 받지 않는다.
    // transient_local 이라 나중에 붙어도 마지막 값을 받는다.
    authoritySub_ = create_subscription<shalom_interfaces::msg::MotionAuthority>(
        "/motion/authority", rclcpp::QoS(1).transient_local(),
        [this](const shalom_interfaces::msg::MotionAuthority::SharedPtr msg) {
            const std::string next = authorityName(msg->state);
            if (motionAuthority_ == next)
                return;
            motionAuthority_ = next;
            // 자세 버튼을 잠그고 푸는 근거가 이 값이다. 바뀐 것을 보내지
            // 않으면 화면은 팔이 멈춘 뒤에도 버튼을 잠근 채로 둔다. 관제가
            // 없을 때 쌓아 두지는 않는다 — 새로 붙는 화면에는 접속 시점에
            // 현재 값을 한 번 밀어 준다(publishHealth).
            if (server_.isConnected())
                publishBase();
        });
    safetyStateSub_ = create_subscription<shalom_interfaces::msg::SafetyState>(
        "/safety/state", rclcpp::QoS(1).transient_local(),
        [this](const shalom_interfaces::msg::SafetyState::SharedPtr msg) {
            const std::string next = safetyStateName(msg->state);
            const bool estop_active = msg->software_estop_active || msg->physical_estop_active;
            const bool changed = safetyState_ != next || estopEngaged_ != estop_active;
            safetyState_ = next;
            estopEngaged_ = estop_active;
            if (!changed)
                return;
            publishSafety();
        });
    missionStateSub_ = create_subscription<shalom_interfaces::msg::MissionState>(
        "/mission/state", rclcpp::QoS(1).transient_local(),
        std::bind(&BridgeNode::onMissionState, this, std::placeholders::_1));

    linkTimer_ = create_wall_timer(10ms, [this] { pollLink(); });
    poseTimer_ = create_wall_timer(100ms, [this] { publishPose(); });     // 10 Hz
    safetyTimer_ = create_wall_timer(50ms, [this] {
        tickSafety();
        publishSafety();
    });                                                                    // 20 Hz
    healthTimer_ = create_wall_timer(1s, [this] { publishHealth(); });
    navTimer_ = create_wall_timer(200ms, [this] { publishNav(); });       // 5 Hz
    systemTimer_ = create_wall_timer(1s, [this] { publishSystem(); });
    trailTimer_ = create_wall_timer(500ms, [this] { publishTrail(); });   // 2 Hz

    // 팔은 이름으로 지시한다. 받는 쪽(시뮬레이터든 실기 드라이버든)이 자기
    // 순서를 쓰므로, 색인으로 보내면 언젠가 다른 축이 움직인다.
    // HMI arm goals are merely one source. joint_mux arbitrates them before
    // safety_gate; this node must never publish straight to an FR3 driver.
    armCmdPub_ = create_publisher<sensor_msgs::msg::JointState>(
        "/motion/arm/joint_command/manual_hold", 10);

    // 본체의 수동 제자리. 조작자가 수동으로 넘긴 동안 20 Hz 로 0 을 내보내
    // twist_mux 의 manual_hold(90) 자리를 잡아 둔다. 이것이 없으면 수동으로
    // 바꿔도 mux 가 nav(20) 로 내려가 로봇이 목표를 향해 계속 간다.
    baseHoldPub_ = create_publisher<geometry_msgs::msg::Twist>(
        "/motion/manual_hold/cmd_vel", 10);

    // ---- 촬영 -------------------------------------------------------------
    captureEnabled_ = declare_parameter("capture.enabled", false);
    if (captureEnabled_) {
        spoolDir_ = declare_parameter("capture.spool_dir",
                                      std::string(std::getenv("HOME") ? std::getenv("HOME") : "/tmp")
                                          + "/inspection");
        maxCaptureLinear_ = declare_parameter("capture.max_linear_speed", maxCaptureLinear_);
        maxCaptureAngular_ = declare_parameter("capture.max_angular_speed", maxCaptureAngular_);
        const auto colorTopic = declare_parameter(
            "capture.color_topic", std::string("/fr3/camera_2d/image_raw"));
        const auto depthTopic = declare_parameter(
            "capture.depth_topic",
            std::string("/fr3/camera_3d/image_raw"));

        colorSub_ = create_subscription<sensor_msgs::msg::Image>(
            colorTopic, rclcpp::SensorDataQoS(),
            [this](const sensor_msgs::msg::Image::ConstSharedPtr &msg) {
                lastColor_ = msg;
            });
        depthSub_ = create_subscription<sensor_msgs::msg::Image>(
            depthTopic, rclcpp::SensorDataQoS(),
            [this](const sensor_msgs::msg::Image::ConstSharedPtr &msg) { lastDepth_ = msg; });
    }


    // 액션 이름은 Nav2 기본값이다. 다른 이름을 쓰는 스택에 붙일 때는 런치에서
    // 리매핑한다 — 여기에 파라미터를 하나 더 만들면 프로토콜 문서와 어긋날
    // 자리가 하나 더 생긴다.
    navClient_ = rclcpp_action::create_client<NavigateToPose>(this, "navigate_to_pose");
    mapLoadClient_ = create_client<nav2_msgs::srv::LoadMap>("map_server/load_map");

    // 감시할 센서 목록은 파라미터에서 온다 (bridge.yaml 의 sensors).
    for (const auto &id : declare_parameter<std::vector<std::string>>(
             "sensors", std::vector<std::string>{})) {
        Sensor sensor;
        sensor.id = id;
        sensor.name = declare_parameter<std::string>("sensor." + id + ".name", id);
        sensor.topic = declare_parameter<std::string>("sensor." + id + ".topic", "");
        sensor.type = declare_parameter<std::string>("sensor." + id + ".type", "");
        sensor.expectedHz = declare_parameter<double>("sensor." + id + ".expected_hz", 0.0);
        sensor.lastSeen = now();
        if (sensor.topic.empty() || sensor.type.empty()) {
            RCLCPP_WARN(get_logger(), "센서 %s: topic 또는 type 이 없어 건너뛴다", id.c_str());
            continue;
        }
        sensors_.push_back(std::move(sensor));
    }
    // 구독은 목록이 다 자란 뒤에 건다. 벡터가 커지면서 재할당되면 콜백이
    // 붙잡은 참조가 무효가 된다.
    for (auto &sensor : sensors_) {
        // 내용은 필요 없고 도착 시각만 필요하다. 제네릭 구독을 쓰면 센서마다
        // 메시지 타입을 컴파일 시점에 알 필요가 없어, 카메라가 늘어도 이
        // 노드는 그대로다.
        sensor.sub = create_generic_subscription(
            sensor.topic, sensor.type, rclcpp::SensorDataQoS(),
            [this, &sensor](std::shared_ptr<const rclcpp::SerializedMessage>) {
                markSeen(sensor);
            });
    }
    RCLCPP_INFO(get_logger(), "센서 %zu 개 감시", sensors_.size());
}

BridgeNode::~BridgeNode()
{
    server_.stop();
}

// ================= 링크 =================

void BridgeNode::pollLink()
{
    const LinkEvents events = server_.drain();
    if (events.empty())
        return;

    if (events.clientConnected) {
        RCLCPP_INFO(get_logger(), "관제 연결됨");
    }
    if (events.protocolError) {
        // 프레이밍이 어긋났다는 것은 링크나 양쪽 구현 중 하나에 문제가
        // 있다는 뜻이다. 조용히 넘기면 안 된다.
        RCLCPP_ERROR(get_logger(), "프로토콜 오류로 연결을 끊었습니다: %s",
                     events.protocolErrorDetail.c_str());
        // 이미 끊긴 뒤라 이 이벤트는 다음 연결에서야 전달된다. 그래도 보내는
        // 이유는, 재연결한 관제가 직전에 무슨 일이 있었는지 알아야 하기 때문이다.
        sendEnvelope(makeEvent(kChLog, json{{"code", "LINK_FRAME_CORRUPT"},
                                            {"level", "error"},
                                            {"msg", events.protocolErrorDetail}}));
    }
    if (events.clientDisconnected) {
        RCLCPP_WARN(get_logger(), "관제 연결 끊김");
    }

    for (const auto &frame : events.frames)
        handleFrame(frame);
}

void BridgeNode::handleFrame(const inspection::Frame &frame)
{
    std::string err;
    const auto env = Envelope::fromHeader(frame.header, &err);
    if (!env) {
        // 버전 불일치를 포함한다. 어긋난 채로 운용하는 것이 가장 위험하다.
        RCLCPP_ERROR(get_logger(), "봉투 해석 실패: %s", err.c_str());
        return;
    }

    // 나를 지목하지 않은 프레임은 실행하지 않는다.
    //
    // 관제가 여러 로봇을 다루게 되면 주소를 잘못 적어 옆 로봇에 붙는 일이
    // 생긴다. 그 상태에서도 화면은 정상으로 보이므로 조작자는 알아채지
    // 못하고, 비상정지를 누르면 아무도 보고 있지 않은 로봇이 선다.
    //
    // 빈 값은 "이 연결에 있는 로봇" 이라는 뜻으로 통과시킨다. 관제는 식별자를
    // 듣기 전까지 비워 보내므로, 첫 명령이 식별자가 없다는 이유로 거절되는
    // 일은 없다.
    if (!env->robot.empty() && env->robot != robotId_) {
        if (env->t == mtype::kReq) {
            // 응답에는 우리 식별자가 찍혀 나가므로, 관제는 자기가 실제로
            // 어느 로봇에 닿았는지 알게 된다.
            respond(*env, false, err::kRobotMismatch,
                    "이 로봇은 " + robotId_ + " 입니다. " + env->robot
                        + " 로 보낸 명령은 실행하지 않습니다");
        }
        RCLCPP_WARN_THROTTLE(
            get_logger(), *get_clock(), 5000,
            "%s 로 보낸 %s 를 무시했다. 이 로봇은 %s 다", env->robot.c_str(),
            env->ch.empty() ? env->t.c_str() : env->ch.c_str(), robotId_.c_str());
        return;
    }

    if (env->t == mtype::kHb)
        handleHeartbeat(*env);
    else if (env->t == mtype::kReq)
        handleRequest(*env);
    else if (env->t == mtype::kPub && env->ch == "cmd/cmd_vel")
        RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000,
                             "TCP cmd/cmd_vel은 비활성입니다. 수동 조작은 UDP teleop_bridge를 사용합니다");
    else if (env->t == mtype::kSub)
        RCLCPP_INFO(get_logger(), "구독 요청 수신");
}

void BridgeNode::handleHeartbeat(const Envelope &heartbeat)
{
    // 같은 seq 를 돌려보내 관제가 왕복 지연을 잴 수 있게 한다.
    sendEnvelope(makeHeartbeat(heartbeat.p.value("seq", std::int64_t{0})));
}

void BridgeNode::sendEnvelope(const Envelope &env, bool lossy, const std::string &payload)
{
    // 나가는 모든 프레임에 자기 식별자를 찍는다. 한 곳에서 하는 이유는,
    // 채널을 새로 추가하는 사람이 이것을 기억해야 한다면 언젠가 빠지기
    // 때문이다 — 빠진 프레임은 관제 쪽에서 "아직 모르는 로봇" 으로 보여
    // 증상이 없다.
    Envelope stamped = env;
    stamped.robot = robotId_;
    server_.send(inspection::encodeFrame(stamped.toHeader(), payload), lossy);
}

void BridgeNode::respond(const Envelope &request, bool ok, const std::string &code,
                         const std::string &message)
{
    sendEnvelope(makeResponse(request, ok, code, message));
}

// ================= 명령 =================

bool BridgeNode::commandsAllowed(const Envelope &request)
{
    if (estopActive()) {
        respond(request, false, err::kEstopEngaged, "E-Stop 발동 상태입니다");
        return false;
    }
    return true;
}

void BridgeNode::handleRequest(const Envelope &request)
{
    // E-Stop은 일반 API 연결이 아니라 estop_bridge의 전용 TCP 포트만 쓴다.
    // 여기서 받아 버리면 일반 링크가 살아 있는 동안 독립 감시 채널이 죽어도
    // 정지하지 않는, 분리의 목적과 반대되는 구성이 된다.
    if (request.ch == kCmdEstop || request.ch == kCmdEstopRelease) {
        respond(request, false, err::kUnreachable,
                "E-Stop은 estop_bridge 전용 TCP 포트로 보내야 합니다");
        return;
    }
    if (!commandsAllowed(request))
        return;

    if (request.ch == kCmdMode) {
        manualMode_ = request.p.value("mode", std::string("auto")) == "manual";
        if (manualMode_) {
            pauseMissionForManualTakeover();
            requestBaseAuthority();
        }
        RCLCPP_INFO(get_logger(), "주행 모드: %s", manualMode_ ? "수동" : "자율");
        respond(request, true);
        return;
    }

    if (request.ch == kCmdNavCancel) {
        // E-Stop 을 치지 않고 자율주행만 멈추는 유일한 길이다. 이것이 없으면
        // 조작자는 비상정지를 누르거나 수동 모드로 넘기는 수밖에 없고, 둘 다
        // 그냥 "여기서 서라" 보다 훨씬 큰 조치다.
        cancelNavigation("관제 취소 요청");
        respond(request, true);
        return;
    }

    if (request.ch == kCmdBasePosture) {
        handleBasePosture(request);
        return;
    }

    if (request.ch == kCmdArmStop) {
        // 지금 있는 자리를 그대로 목표로 준다. 명령을 끊는 것만으로는 팔이
        // 마지막 목표를 향해 계속 간다.
        sensor_msgs::msg::JointState hold;
        hold.header.stamp = now();
        hold.name = kArmJointNames;
        hold.position = lastArmPositions_;
        if (hold.position.size() == kArmJointNames.size())
            armCmdPub_->publish(hold);
        respond(request, true);
        return;
    }

    if (request.ch == kCmdArmPreset) {
        const auto name = request.p.value("name", std::string{});
        const auto *preset = armPreset(name);
        if (preset == nullptr) {
            respond(request, false, err::kBadPayload, "알 수 없는 프리셋: " + name);
            return;
        }
        applyArmGoal(request, *preset);
        return;
    }

    if (request.ch == kCmdArmJointGoal) {
        std::vector<double> q;
        for (const auto &v : request.p.value("positions", json::array())) {
            if (!v.is_number()) {
                respond(request, false, err::kBadPayload, "관절 값이 숫자가 아닙니다");
                return;
            }
            q.push_back(v.get<double>());
        }
        applyArmGoal(request, q);
        return;
    }

    if (request.ch == kCmdArmEeGoal) {
        // 끝단 목표는 역기구학이 필요하고, 그 판단은 MoveIt2 의 몫이다
        // (프로토콜 §4). 아직 붙지 않았다는 사실을 조용히 넘기지 않는다 —
        // 조작자가 보내고 아무 일도 일어나지 않으면 원인을 짚을 수 없다.
        respond(request, false, err::kUnreachable,
                "끝단 목표는 아직 연결되지 않았습니다 (MoveIt2 미연동)");
        return;
    }

    if (request.ch == kCmdWaypointsSet) {
        if (haveMissionState_ && missionState_.state != shalom_interfaces::msg::MissionState::IDLE &&
            missionState_.state != shalom_interfaces::msg::MissionState::COMPLETED &&
            missionState_.state != shalom_interfaces::msg::MissionState::FAILED) {
            respond(request, false, err::kBusy, "미션이 끝난 뒤 점검 지점을 변경하십시오");
            return;
        }
        waypoints_ = request.p.value("points", json::array());
        ++missionPlanRevision_;
        saveMapState("waypoints.json", waypoints_);
        respond(request, true);
        publishWaypoints();
        publishMapCatalog();
        RCLCPP_INFO(get_logger(), "점검 지점 %zu 개 수신", waypoints_.size());
        return;
    }

    if (request.ch == kCmdLocationsSet) {
        if (haveMissionState_ && missionState_.state != shalom_interfaces::msg::MissionState::IDLE &&
            missionState_.state != shalom_interfaces::msg::MissionState::COMPLETED &&
            missionState_.state != shalom_interfaces::msg::MissionState::FAILED) {
            respond(request, false, err::kBusy, "미션이 끝난 뒤 위치를 변경하십시오");
            return;
        }
        locations_ = request.p.value("locations", json::array());
        ++missionPlanRevision_;
        saveMapState("locations.json", locations_);
        respond(request, true);
        publishLocations();
        return;
    }

    if (request.ch == kCmdMarkersSet) {
        // 측량해 넣은 마커 자리다. 관제에서 통째로 갈아 끼우고, 로봇은 그것을
        // 그대로 들고 있다가 되돌려 준다 — 어느 태그가 어디 붙어 있는지는
        // 사람이 재어 오는 값이라 로봇이 스스로 정할 수 있는 것이 아니다.
        markers_ = request.p.value("markers", json::array());
        saveMapState("markers.json", markers_);
        respond(request, true);
        publishMarkers();
        RCLCPP_INFO(get_logger(), "마커 %zu개 등록", markers_.size());
        return;
    }

    if (request.ch == kCmdMapsList) {
        respond(request, true);
        publishMapCatalog();
        publishActiveMap();
        return;
    }

    if (request.ch == kCmdMapsSelect) {
        const std::string id = request.p.value("id", std::string{});
        if (!haveMissionState_ || missionState_.state != shalom_interfaces::msg::MissionState::IDLE ||
            navGoal_) {
            respond(request, false, err::kBusy,
                    "주행 또는 점검이 끝난 뒤에 지도를 전환하십시오");
            return;
        }
        if (id.empty() || id.find('/') != std::string::npos || id.find("..") != std::string::npos
            || !std::filesystem::is_regular_file(std::filesystem::path(mapsDir_) / id / "map.yaml")) {
            respond(request, false, err::kBadPayload, "유효하지 않거나 없는 지도입니다");
            return;
        }
        if (!mapLoadClient_->service_is_ready()) {
            respond(request, false, err::kHardware,
                    "map_server/load_map 서비스를 찾지 못했습니다");
            return;
        }

        auto load = std::make_shared<nav2_msgs::srv::LoadMap::Request>();
        load->map_url = (std::filesystem::path(mapsDir_) / id / "map.yaml").string();
        mapLoadClient_->async_send_request(load, [this, request, id](
                                                rclcpp::Client<nav2_msgs::srv::LoadMap>::SharedFuture future) {
            const auto result = future.get();
            if (result->result != nav2_msgs::srv::LoadMap::Response::RESULT_SUCCESS) {
                respond(request, false, err::kHardware, "map_server가 지도를 불러오지 못했습니다");
                return;
            }
            // loadMapBundle은 요청 전에 검증했지만, 서비스 응답이 돌아오는 동안
            // 파일이 바뀌었을 수도 있으므로 상태도 성공 시점에 다시 읽는다.
            std::string detail;
            if (!loadMapBundle(id, &detail)) {
                respond(request, false, err::kHardware, detail);
                return;
            }
            publishActiveMap();
            publishMapCatalog();
            publishWaypoints();
            publishLocations();
            publishMarkers();
            respond(request, true);
        });
        return;
    }

    if (request.ch == kCmdMapsRename) {
        const std::string id = request.p.value("id", std::string{});
        const std::string name = request.p.value("name", std::string{});
        if (!haveMissionState_ || missionState_.state != shalom_interfaces::msg::MissionState::IDLE ||
            navGoal_) {
            respond(request, false, err::kBusy,
                    "주행 또는 점검이 끝난 뒤에 지도 이름을 바꾸십시오");
            return;
        }
        if (id.empty() || id.find('/') != std::string::npos || id.find("..") != std::string::npos
            || !std::filesystem::is_regular_file(std::filesystem::path(mapsDir_) / id / "map.yaml")) {
            respond(request, false, err::kBadPayload, "유효하지 않거나 없는 지도입니다");
            return;
        }
        // 이름은 UTF-8 바이트 기준으로 제한한다. 줄바꿈을 허용하면 UI와 로그의
        // 한 항목 경계가 깨지므로 표시 이름으로 쓸 수 없다.
        if (name.empty() || name.size() > 120 || name.find('\n') != std::string::npos
            || name.find('\r') != std::string::npos) {
            respond(request, false, err::kBadPayload, "지도 이름은 줄바꿈 없는 1~120바이트여야 합니다");
            return;
        }

        const std::filesystem::path dir = std::filesystem::path(mapsDir_) / id;
        const std::filesystem::path metadata_path = dir / "metadata.json";
        json metadata{{"schema_version", 1}, {"id", id}};
        try {
            std::ifstream in(metadata_path);
            if (in)
                in >> metadata;
        } catch (const json::parse_error &e) {
            RCLCPP_ERROR(get_logger(), "지도 메타데이터가 손상되어 이름을 바꾸지 않습니다 (%s): %s",
                         metadata_path.c_str(), e.what());
            respond(request, false, err::kHardware, "지도 메타데이터를 읽지 못했습니다");
            return;
        }
        if (!metadata.is_object()) {
            RCLCPP_ERROR(get_logger(), "지도 메타데이터 형식이 객체가 아닙니다: %s",
                         metadata_path.c_str());
            respond(request, false, err::kHardware, "지도 메타데이터 형식이 올바르지 않습니다");
            return;
        }
        metadata["schema_version"] = 1;
        metadata["id"] = id;
        metadata["name"] = name;

        // 같은 디렉터리에 완성본을 쓴 뒤 rename한다. 전원 장애나 HMI 연결이
        // 끊겨도 metadata.json이 반쯤 쓰인 상태로 남지 않게 한다.
        const std::filesystem::path temporary_path = dir / ".metadata.json.tmp";
        {
            std::ofstream out(temporary_path, std::ios::trunc);
            if (!out) {
                respond(request, false, err::kHardware, "지도 메타데이터를 쓸 수 없습니다");
                return;
            }
            out << metadata.dump(2) << '\n';
            if (!out) {
                std::error_code remove_ec;
                std::filesystem::remove(temporary_path, remove_ec);
                respond(request, false, err::kHardware, "지도 메타데이터 저장에 실패했습니다");
                return;
            }
        }
        std::error_code rename_ec;
        std::filesystem::rename(temporary_path, metadata_path, rename_ec);
        if (rename_ec) {
            std::error_code remove_ec;
            std::filesystem::remove(temporary_path, remove_ec);
            RCLCPP_ERROR(get_logger(), "지도 메타데이터 교체 실패 (%s): %s",
                         metadata_path.c_str(), rename_ec.message().c_str());
            respond(request, false, err::kHardware, "지도 메타데이터를 반영하지 못했습니다");
            return;
        }

        publishMapCatalog();
        if (id == mapId_)
            publishActiveMap();
        respond(request, true);
        return;
    }

    if (request.ch == kCmdCapture) {
        handleCapture(request);
        return;
    }

    if (request.ch == kCmdMissionStart) {
        configureAndStartMission(request);
        return;
    }

    if (request.ch == kCmdMissionPause) {
        sendMissionControl(request, shalom_interfaces::srv::MissionControl::Request::PAUSE);
        return;
    }

    if (request.ch == kCmdMissionResume) {
        if (estopActive()) {
            respond(request, false, err::kMode,
                    "비상정지 상태입니다. 해제한 뒤 재개하십시오");
            return;
        }
        sendMissionControl(request, shalom_interfaces::srv::MissionControl::Request::RESUME);
        return;
    }

    if (request.ch == kCmdMissionStop) {
        const uint8_t operation = haveMissionState_ &&
            (missionState_.state == shalom_interfaces::msg::MissionState::COMPLETED ||
             missionState_.state == shalom_interfaces::msg::MissionState::FAILED)
            ? shalom_interfaces::srv::MissionControl::Request::RESET
            : shalom_interfaces::srv::MissionControl::Request::STOP;
        sendMissionControl(request, operation);
        return;
    }

    if (request.ch == kCmdPowerPolicy) {
        returnAtPct_ = request.p.value("return_at", returnAtPct_);
        departAtPct_ = request.p.value("depart_at", departAtPct_);
        respond(request, true);
        RCLCPP_INFO(get_logger(), "배터리 정책: 복귀 %.0f%%, 출발 %.0f%%",
                    returnAtPct_, departAtPct_);
        return;
    }

    if (request.ch == kCmdGoto) {
        if (manualMode_) {
            respond(request, false, err::kMode, "수동 모드에서는 자율 이동을 실행하지 않습니다");
            return;
        }
        if (haveMissionState_ &&
            missionState_.state != shalom_interfaces::msg::MissionState::IDLE &&
            missionState_.state != shalom_interfaces::msg::MissionState::COMPLETED &&
            missionState_.state != shalom_interfaces::msg::MissionState::FAILED) {
            respond(request, false, err::kBusy, "미션이 끝난 뒤 개별 목표를 지정하십시오");
            return;
        }
        requestSafetyResume();
        requestBaseAuthority();
        startNavigation(request);
        return;
    }

    // 미지원 채널은 조용히 무시하지 않고 사유를 돌려준다. 관제가 새 기능을
    // 쓰려다 아무 반응이 없으면 원인을 짚을 수 없다.
    respond(request, false, err::kUnknownChannel, "지원하지 않는 채널: " + request.ch);
}

namespace {

/// First value in /proc/<file> matching `key`, in whatever unit the file uses.
double readMetric(const char *path, const char *key)
{
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind(key, 0) != 0)
            continue;
        const auto pos = line.find_first_of("0123456789");
        if (pos == std::string::npos)
            return 0.0;
        return std::strtod(line.c_str() + pos, nullptr);
    }
    return 0.0;
}

/// Hottest CPU-ish thermal zone, in Celsius.
///
/// Which zone is the CPU differs between boards, and several of them read
/// room temperature. Taking the maximum finds the one that will throttle
/// first, which is the number worth showing.
double cpuTemperature()
{
    double hottest = 0.0;
    for (int i = 0; i < 32; ++i) {
        std::ifstream f("/sys/class/thermal/thermal_zone" + std::to_string(i) + "/temp");
        if (!f)
            break;
        double milli = 0.0;
        f >> milli;
        hottest = std::max(hottest, milli / 1000.0);
    }
    return hottest;
}

}  // namespace

void BridgeNode::publishSystem()
{
    if (!server_.isConnected())
        return;

    // CPU is a delta against the previous second; one reading of /proc/stat
    // only gives the average since boot, which barely moves after an hour.
    double cpuPct = 0.0;
    {
        std::ifstream f("/proc/stat");
        std::string cpu;
        unsigned long long user = 0, nice = 0, sys = 0, idle = 0, iowait = 0,
                           irq = 0, softirq = 0, steal = 0;
        if (f >> cpu >> user >> nice >> sys >> idle >> iowait >> irq >> softirq >> steal) {
            const unsigned long long idleAll = idle + iowait;
            const unsigned long long total =
                user + nice + sys + idleAll + irq + softirq + steal;
            if (cpuTotal_ != 0 && total > cpuTotal_) {
                const double dTotal = double(total - cpuTotal_);
                const double dIdle = double(idleAll - cpuIdle_);
                cpuPct = 100.0 * (dTotal - dIdle) / dTotal;
            }
            cpuIdle_ = idleAll;
            cpuTotal_ = total;
        }
    }

    const double memTotal = readMetric("/proc/meminfo", "MemTotal:");
    const double memAvail = readMetric("/proc/meminfo", "MemAvailable:");
    const double memPct = memTotal > 0.0 ? 100.0 * (1.0 - memAvail / memTotal) : 0.0;

    // GPU is left null rather than guessed. On the Jetson it comes from the
    // tegra counters and on a desktop from NVML; neither is worth shelling out
    // to once a second, and a zero would read as "idle" instead of "unknown".
    sendEnvelope(makePublish(kChSystem,
                             json{{"cpu_pct", cpuPct},
                                  {"mem_pct", memPct},
                                  {"cpu_temp_c", cpuTemperature()},
                                  {"gpu_pct", nullptr},
                                  {"gpu_temp_c", nullptr},
                                  {"net_rtt_ms", nullptr},
                                  {"net_rssi", nullptr},
                                  {"robot_id", robotId_},
                                  {"robot_name", robotName_}}),
                 true);
}

const std::vector<double> *BridgeNode::armPreset(const std::string &name)
{
    // 관제의 hmi/src/RobotDef.h 와 같은 값이다. FAIRINO 의 URDF 를 훑어
    // 가동 한계·조작성·데크 간섭·LiDAR 간섭 네 조건을 만족하도록 고른 자세다.
    static const std::vector<double> kHome{0.0, -1.600, -0.800, -1.400, -1.571, 0.0};
    static const std::vector<double> kStandby{0.0, -1.200, -2.200, -1.800, -1.571, 0.0};
    static const std::vector<double> kStow{0.0, -2.600, -2.400, 0.400, -1.571, 0.0};
    if (name == "home") return &kHome;
    if (name == "standby") return &kStandby;
    if (name == "stow") return &kStow;
    return nullptr;
}

bool BridgeNode::applyArmGoal(const Envelope &request, const std::vector<double> &positions)
{
    if (positions.size() != kArmJointNames.size()) {
        respond(request, false, err::kBadPayload,
                "관절 수가 맞지 않습니다: " + std::to_string(positions.size()));
        return false;
    }
    sensor_msgs::msg::JointState msg;
    msg.header.stamp = now();
    msg.name = kArmJointNames;
    msg.position = positions;
    armCmdPub_->publish(msg);
    respond(request, true);
    return true;
}

void BridgeNode::publishWaypoints()
{
    sendEnvelope(makePublish(kChWaypoints, json{{"points", waypoints_}}));
}

void BridgeNode::publishLocations()
{
    sendEnvelope(makePublish(kChLocations, json{{"locations", locations_}}));
}

void BridgeNode::publishActiveMap()
{
    json active{{"id", mapId_}, {"name", mapId_}};
    const auto meta_path = std::filesystem::path(mapsDir_) / mapId_ / "metadata.json";
    std::ifstream meta_in(meta_path);
    json meta;
    try {
        if (meta_in >> meta)
            active["name"] = meta.value("name", mapId_);
    } catch (const json::parse_error &e) {
        RCLCPP_WARN(get_logger(), "지도 메타데이터를 읽지 못했습니다 (%s): %s",
                    meta_path.c_str(), e.what());
    }
    sendEnvelope(makePublish(kChActiveMap, active));
}

void BridgeNode::publishMapCatalog()
{
    json maps = json::array();
    std::error_code ec;
    const std::filesystem::path root(mapsDir_);
    for (const auto &entry : std::filesystem::directory_iterator(root, ec)) {
        if (ec || !entry.is_directory())
            continue;
        const auto dir = entry.path();
        if (!std::filesystem::is_regular_file(dir / "map.yaml"))
            continue;

        const std::string id = dir.filename().string();
        json item{{"id", id}, {"name", id}, {"active", id == mapId_},
                  {"waypoint_count", 0}};
        try {
            std::ifstream meta_in(dir / "metadata.json");
            json meta;
            if (meta_in >> meta) {
                item["name"] = meta.value("name", id);
                item["created_at"] = meta.value("created_at", std::string{});
            }
            std::ifstream wp_in(dir / "waypoints.json");
            json waypoints;
            if (wp_in >> waypoints && waypoints["points"].is_array())
                item["waypoint_count"] = waypoints["points"].size();
        } catch (const json::parse_error &e) {
            RCLCPP_WARN(get_logger(), "지도 카탈로그 항목을 읽지 못했습니다 (%s): %s",
                        dir.c_str(), e.what());
        }
        maps.push_back(std::move(item));
    }
    // 날짜 기반 ID는 문자열 정렬 자체가 시간순이다. directory_iterator의 순서는
    // 파일 시스템마다 달라 HMI 목록까지 흔들리므로 여기서 고정한다.
    std::sort(maps.begin(), maps.end(), [](const json &left, const json &right) {
        return left.value("id", std::string{}) < right.value("id", std::string{});
    });
    sendEnvelope(makePublish(kChMaps, json{{"maps", maps}}));
}

bool BridgeNode::loadMapBundle(const std::string &map_id, std::string *error)
{
    if (map_id.empty() || map_id.find('/') != std::string::npos || map_id.find("..") != std::string::npos) {
        if (error) *error = "유효하지 않은 지도 ID";
        return false;
    }
    const std::filesystem::path dir = std::filesystem::path(mapsDir_) / map_id;
    if (!std::filesystem::is_regular_file(dir / "map.yaml")) {
        if (error) *error = "지도 파일이 없습니다";
        return false;
    }

    const auto read = [this, &dir](const char *name, const char *key) {
        std::ifstream in(dir / name);
        json out = json::array();
        json doc;
        try {
            if (in >> doc && doc[key].is_array())
                out = doc[key];
        } catch (const json::parse_error &e) {
            RCLCPP_WARN(get_logger(), "지도 상태를 읽지 못했습니다 (%s): %s",
                        (dir / name).c_str(), e.what());
        }
        return out;
    };
    waypoints_ = read("waypoints.json", "points");
    locations_ = read("locations.json", "locations");
    markers_ = read("markers.json", "markers");
    mapId_ = map_id;
    return true;
}

bool BridgeNode::saveMapState(const char *filename, const json &state)
{
    if (mapId_ == "live")
        return false;
    const std::filesystem::path path = std::filesystem::path(mapsDir_) / mapId_ / filename;
    std::ofstream out(path);
    if (!out)
        return false;
    const std::string key = std::string(filename) == "waypoints.json" ? "points"
                            : std::string(filename) == "locations.json" ? "locations"
                                                                          : "markers";
    json document{{"schema_version", 1}, {"map_id", mapId_}};
    document[key] = state;
    out << document.dump(2);
    return bool(out);
}


bool BridgeNode::isMoving() const
{
    // 오도메트리가 끊겼으면 움직이는 것으로 본다. 모르는 채로 찍어 흔들린
    // 사진을 남기는 것보다, 거절하고 이유를 말하는 편이 낫다.
    if (lastOdomAt_.nanoseconds() == 0 || (now() - lastOdomAt_).seconds() > 1.0)
        return true;
    return speedLinear_ > maxCaptureLinear_ || speedAngular_ > maxCaptureAngular_;
}

double BridgeNode::centreDistanceMm() const
{
    // 정렬된 깊이 이미지 한가운데 값. 대상이 화면 중앙에 있다는 가정인데,
    // Apriltag 보정이 붙기 전까지는 이보다 나은 근거가 없다. 과업지시서의
    // 필수 메타데이터에 촬영거리가 있어 빈 값으로 둘 수도 없다.
    if (!lastDepth_ || lastDepth_->encoding != "16UC1")
        return -1.0;
    const auto w = lastDepth_->width, h = lastDepth_->height;
    if (w == 0 || h == 0)
        return -1.0;
    const std::size_t offset = std::size_t(h / 2) * lastDepth_->step
                               + std::size_t(w / 2) * 2;
    if (offset + 1 >= lastDepth_->data.size())
        return -1.0;
    const std::uint16_t mm = std::uint16_t(lastDepth_->data[offset])
                             | std::uint16_t(lastDepth_->data[offset + 1] << 8);
    return mm == 0 ? -1.0 : double(mm);   // 0 은 "측정 못 함" 이다
}

void BridgeNode::publishCaptureSpool()
{
    std::error_code ec;
    const auto space = std::filesystem::space(spoolDir_, ec);
    sendEnvelope(makePublish(kChCaptureSpool,
                             json{{"nas_online", !ec},
                                  {"pending", capturesTaken_},
                                  {"spool_free_mb",
                                   ec ? 0.0 : double(space.available) / (1024.0 * 1024.0)}}));
}

void BridgeNode::handleCapture(const Envelope &request)
{
    if (!captureEnabled_) {
        respond(request, false, err::kMode,
                "이 배포본에는 카메라 촬영 기능이 포함되어 있지 않습니다");
        return;
    }
    if (!lastColor_) {
        respond(request, false, err::kUnreachable,
                "카메라 영상이 없습니다. 카메라 연결을 확인하십시오");
        return;
    }
    if (estopActive()) {
        respond(request, false, err::kMode, "비상정지 상태에서는 촬영하지 않습니다");
        return;
    }
    // 과업지시서 2.2.4: 반드시 정지 상태에서 촬영한다. 화면에서도 버튼을
    // 잠그지만, 규칙은 로봇이 지켜야 한다 — 화면만 막으면 다른 경로로 들어온
    // 요청은 그대로 통과한다.
    if (isMoving()) {
        respond(request, false, err::kMode,
                "이동 중에는 촬영하지 않습니다. 정지한 뒤 다시 시도하십시오");
        return;
    }

    // 파일명은 과업지시서가 정한 형식이다.
    //   차량번호_량번호_포인트ID,YYYYMMDDHHMMSS.확장자
    const auto text = [&request](const char *key, const char *fallback) {
        const std::string v = request.p.value(key, std::string());
        return v.empty() ? std::string(fallback) : v;
    };
    const std::string vehicle = text("vehicle_number", "UNKNOWN");
    const std::string car = text("car_number", "00");
    const std::string point = text("point_id", "MANUAL");

    const auto t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char stamp[16];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d%H%M%S", &tm);

    const std::string base = vehicle + "_" + car + "_" + point + "," + stamp;

    std::error_code ec;
    std::filesystem::create_directories(spoolDir_, ec);
    if (ec) {
        respond(request, false, err::kHardware,
                "저장 폴더를 만들지 못했습니다: " + spoolDir_);
        return;
    }

    const std::string png = encodeImagePng(*lastColor_);
    if (png.empty()) {
        respond(request, false, err::kHardware,
                "사진을 만들지 못했습니다. 카메라 형식이 " + lastColor_->encoding
                    + " 입니다");
        return;
    }

    const std::string imagePath = spoolDir_ + "/" + base + ".png";
    {
        std::ofstream out(imagePath, std::ios::binary);
        if (!out) {
            respond(request, false, err::kHardware, "사진을 저장하지 못했습니다");
            return;
        }
        out.write(png.data(), std::streamsize(png.size()));
    }

    // 사이드카. 파일명에 담기지 않는 값들이 여기 들어간다 — 과업지시서의
    // 필수 메타데이터 중 로봇좌표, 촬영거리, Apriltag ID 가 그것이다.
    double x = 0.0, y = 0.0, theta = 0.0;
    try {
        const auto tf = tfBuffer_->lookupTransform(mapFrame_, baseFrame_,
                                                   tf2::TimePointZero);
        x = tf.transform.translation.x;
        y = tf.transform.translation.y;
        // 기존 자세 발행부와 같은 방식으로 만든다. tf2::fromMsg 의 이 조합은
        // 헤더에 선언만 있고 구현이 없어 링크가 깨진다.
        const tf2::Quaternion q(tf.transform.rotation.x, tf.transform.rotation.y,
                                tf.transform.rotation.z, tf.transform.rotation.w);
        double roll = 0, pitch = 0, yaw = 0;
        tf2::Matrix3x3(q).getRPY(roll, pitch, yaw);
        theta = yaw;
    } catch (const tf2::TransformException &e) {
        // 자리를 모른 채로도 사진은 남긴다. 좌표가 빠진 사실은 사이드카에
        // 그대로 드러나고, 화면이 그것을 "메타데이터 누락" 으로 표시한다.
        RCLCPP_WARN(get_logger(), "촬영 시각의 자세를 읽지 못했다: %s", e.what());
    }
    const double distance = centreDistanceMm();
    const json meta{
        {"file", base + ".png"},
        {"vehicle_number", vehicle},
        {"car_number", car},
        {"point_id", point},
        {"captured_at", std::string(stamp)},
        {"robot", json{{"x", x}, {"y", y}, {"theta", theta}}},
        {"distance_mm", distance > 0.0 ? json(distance) : json(nullptr)},
        {"tag_id", request.p.contains("tag_id") ? request.p["tag_id"] : json(nullptr)},
        {"map_id", mapId_},
    };
    {
        std::ofstream out(spoolDir_ + "/" + base + ".json");
        out << meta.dump(2);
    }

    ++capturesTaken_;
    // 성공 응답에는 오류 코드가 없다. nullptr 을 넘기면 std::string 이
    // 널에서 만들어져 노드가 죽는다 — 실제로 사진은 저장되고 그 직후
    // 브릿지가 내려앉았다.
    respond(request, true, std::string(), base + ".png");
    RCLCPP_INFO(get_logger(), "촬영 저장: %s (%ux%u %s, PNG %zu 바이트)",
                imagePath.c_str(), lastColor_->width, lastColor_->height,
                lastColor_->encoding.c_str(), png.size());

    // 미리보기는 방금 저장한 그 바이트를 그대로 보낸다. 다시 만들면 화면에
    // 보이는 것과 저장된 것이 다를 수 있다.
    sendEnvelope(makePublish(kChPreview, meta), false, png);
    publishCaptureSpool();
}

// ================= mission adapter =================

std::optional<shalom_interfaces::msg::MissionPlan> BridgeNode::makeMissionPlan(
    std::string *error)
{
    const auto fail = [error](const std::string &message)
        -> std::optional<shalom_interfaces::msg::MissionPlan> {
        if (error)
            *error = message;
        return std::nullopt;
    };
    if (!waypoints_.is_array() || waypoints_.empty())
        return fail("점검포인트가 없습니다");

    shalom_interfaces::msg::MissionPlan plan;
    plan.created_at = now();
    plan.revision = missionPlanRevision_ == 0 ? ++missionPlanRevision_ : missionPlanRevision_;
    plan.map_id = mapId_.empty() ? "live" : mapId_;
    plan.mission_id = plan.map_id + ":" + std::to_string(plan.revision);
    plan.required_capabilities = {"navigation"};

    for (std::size_t i = 0; i < waypoints_.size(); ++i) {
        const auto &point = waypoints_[i];
        if (!point.is_object() || !point.contains("x") || !point.contains("y") ||
            !point["x"].is_number() || !point["y"].is_number()) {
            return fail("점검포인트 " + std::to_string(i + 1) + "의 좌표가 올바르지 않습니다");
        }
        shalom_interfaces::msg::MissionWaypoint waypoint;
        waypoint.waypoint_id = point.value("id", "P" + std::to_string(i + 1));
        waypoint.target_pose.header.frame_id = mapFrame_;
        waypoint.target_pose.header.stamp = now();
        waypoint.target_pose.pose.position.x = point["x"].get<double>();
        waypoint.target_pose.pose.position.y = point["y"].get<double>();
        tf2::Quaternion q;
        q.setRPY(0.0, 0.0, point.value("theta", 0.0));
        waypoint.target_pose.pose.orientation.x = q.x();
        waypoint.target_pose.pose.orientation.y = q.y();
        waypoint.target_pose.pose.orientation.z = q.z();
        waypoint.target_pose.pose.orientation.w = q.w();
        waypoint.operation = shalom_interfaces::msg::MissionWaypoint::NAVIGATE_ONLY;
        plan.waypoints.push_back(std::move(waypoint));
    }

    const int dock = findDock();
    plan.return_to_dock = dock >= 0;
    plan.has_dock_approach = dock >= 0;
    if (dock >= 0) {
        const auto &location = locations_[std::size_t(dock)];
        if (!location.contains("x") || !location.contains("y") ||
            !location["x"].is_number() || !location["y"].is_number()) {
            return fail("충전 스테이션 접근 좌표가 올바르지 않습니다");
        }
        auto &waypoint = plan.dock_approach;
        waypoint.waypoint_id = location.value("id", std::string("dock"));
        waypoint.target_pose.header.frame_id = mapFrame_;
        waypoint.target_pose.header.stamp = now();
        waypoint.target_pose.pose.position.x = location["x"].get<double>();
        waypoint.target_pose.pose.position.y = location["y"].get<double>();
        tf2::Quaternion q;
        q.setRPY(0.0, 0.0, location.value("theta", 0.0));
        waypoint.target_pose.pose.orientation.x = q.x();
        waypoint.target_pose.pose.orientation.y = q.y();
        waypoint.target_pose.pose.orientation.z = q.z();
        waypoint.target_pose.pose.orientation.w = q.w();
        waypoint.operation = shalom_interfaces::msg::MissionWaypoint::NAVIGATE_ONLY;
    }
    return plan;
}

void BridgeNode::configureAndStartMission(const Envelope &request)
{
    if (estopActive()) {
        respond(request, false, err::kMode, "비상정지 상태입니다. 해제한 뒤 시작하십시오");
        return;
    }
    if (!missionConfigureClient_->service_is_ready() ||
        !missionControlClient_->service_is_ready()) {
        respond(request, false, err::kUnreachable, "Mission Manager가 준비되지 않았습니다");
        return;
    }
    if (navGoal_ || pendingGoto_) {
        respond(request, false, err::kBusy, "개별 자율주행 목표가 끝난 뒤 미션을 시작하십시오");
        return;
    }
    std::string detail;
    auto plan = makeMissionPlan(&detail);
    if (!plan) {
        respond(request, false, err::kBadPayload, detail);
        return;
    }

    auto configure = std::make_shared<shalom_interfaces::srv::ConfigureMission::Request>();
    const std::string request_id = request.id.empty()
        ? "hmi-" + std::to_string(++rosRequestSequence_) : request.id;
    configure->request_id = request_id + ":configure";
    configure->operator_id = "hmi";
    configure->plan = *plan;
    missionConfigureClient_->async_send_request(
        configure,
        [this, request, request_id](
            rclcpp::Client<shalom_interfaces::srv::ConfigureMission>::SharedFuture future) {
            const auto configured = future.get();
            if (!configured->accepted) {
                respond(request, false, err::kMode, configured->detail);
                return;
            }
            auto control = std::make_shared<shalom_interfaces::srv::MissionControl::Request>();
            control->request_id = request_id + ":start";
            control->operator_id = "hmi";
            control->mission_id = configured->state.mission_id;
            control->operation = shalom_interfaces::srv::MissionControl::Request::START;
            missionControlClient_->async_send_request(
                control,
                [this, request](
                    rclcpp::Client<shalom_interfaces::srv::MissionControl>::SharedFuture result) {
                    const auto started = result.get();
                    respond(request, started->accepted,
                            started->accepted ? std::string() : err::kMode,
                            started->detail);
                });
        });
}

void BridgeNode::sendMissionControl(const Envelope &request, uint8_t operation)
{
    if (!missionControlClient_->service_is_ready()) {
        respond(request, false, err::kUnreachable, "Mission Manager가 준비되지 않았습니다");
        return;
    }
    auto control = std::make_shared<shalom_interfaces::srv::MissionControl::Request>();
    control->request_id = (request.id.empty()
        ? "hmi-" + std::to_string(++rosRequestSequence_) : request.id) + ":control";
    control->operator_id = "hmi";
    control->mission_id = missionState_.mission_id;
    control->operation = operation;
    missionControlClient_->async_send_request(
        control,
        [this, request](rclcpp::Client<shalom_interfaces::srv::MissionControl>::SharedFuture future) {
            const auto result = future.get();
            respond(request, result->accepted,
                    result->accepted ? std::string() : err::kMode, result->detail);
        });
}

void BridgeNode::pauseMissionForManualTakeover()
{
    if (!haveMissionState_ ||
        (missionState_.state != shalom_interfaces::msg::MissionState::RUNNING &&
         missionState_.state != shalom_interfaces::msg::MissionState::RETURNING))
        return;
    if (!missionControlClient_->service_is_ready()) {
        RCLCPP_ERROR(get_logger(), "수동 전환 중 Mission Manager에 일시정지를 요청하지 못했습니다");
        return;
    }
    auto control = std::make_shared<shalom_interfaces::srv::MissionControl::Request>();
    control->request_id = "hmi-manual-" + std::to_string(++rosRequestSequence_);
    control->operator_id = "hmi_manual_takeover";
    control->mission_id = missionState_.mission_id;
    control->operation = shalom_interfaces::srv::MissionControl::Request::PAUSE;
    missionControlClient_->async_send_request(control);
}

void BridgeNode::onMissionState(
    const shalom_interfaces::msg::MissionState::SharedPtr message)
{
    const uint8_t previous = haveMissionState_ ? missionState_.state
                                               : shalom_interfaces::msg::MissionState::IDLE;
    missionState_ = *message;
    haveMissionState_ = true;
    for (std::size_t i = 0; i < waypoints_.size(); ++i) {
        const bool completed = message->state == shalom_interfaces::msg::MissionState::COMPLETED ||
                               message->state == shalom_interfaces::msg::MissionState::RETURNING;
        const bool before_current = message->current_step >= 0 &&
                                    i < std::size_t(message->current_step);
        const bool current = message->current_step >= 0 &&
                             i == std::size_t(message->current_step);
        waypoints_[i]["status"] = completed || before_current ? "done"
            : current && message->state == shalom_interfaces::msg::MissionState::FAILED ? "error"
            : current ? "current" : "todo";
    }
    publishWaypoints();
    publishMission();
    if (message->state == shalom_interfaces::msg::MissionState::COMPLETED &&
        previous != message->state) {
        sendEnvelope(makeEvent(kChLog, json{{"code", "MISSION_COMPLETE"}}));
    }
}

/// 등록된 위치에서 충전 스테이션을 찾는다. 없으면 -1.
int BridgeNode::findDock() const
{
    for (std::size_t i = 0; i < locations_.size(); ++i) {
        const auto &loc = locations_[i];
        if (loc.value("kind", std::string()) == "dock")
            return int(i);
    }
    return -1;
}

void BridgeNode::publishMission()
{
    const uint8_t state = haveMissionState_ ? missionState_.state
        : shalom_interfaces::msg::MissionState::IDLE;
    sendEnvelope(makePublish(kChMission,
                             json{{"state", missionStateName(state)},
                                  {"index", haveMissionState_ ? missionState_.current_step : -1},
                                  {"total", haveMissionState_ ? missionState_.total_steps : 0},
                                  {"reason_code", haveMissionState_
                                      ? missionState_.reason_code : std::string()}}));
}






void BridgeNode::publishMarkers()
{
    sendEnvelope(makePublish(kChMarkers, json{{"markers", markers_}}));
}

void BridgeNode::publishTrail()
{
    if (!server_.isConnected())
        return;

    geometry_msgs::msg::TransformStamped tf;
    try {
        tf = tfBuffer_->lookupTransform(mapFrame_, baseFrame_, tf2::TimePointZero);
    } catch (const tf2::TransformException &) {
        return;
    }
    const double x = tf.transform.translation.x;
    const double y = tf.transform.translation.y;

    if (!trailHasLast_ || std::hypot(x - trailLastX_, y - trailLastY_) >= kTrailMinStepM) {
        trailPending_.emplace_back(x, y);
        trailLastX_ = x;
        trailLastY_ = y;
        trailHasLast_ = true;
    }
    if (trailPending_.empty() && !trailReset_)
        return;

    json points = json::array();
    for (const auto &pt : trailPending_)
        points.push_back({pt.first, pt.second});
    sendEnvelope(makePublish(kChTrail, json{{"points", points}, {"reset", trailReset_}}), true);
    trailPending_.clear();
    trailReset_ = false;
}

namespace {

/// One PNG chunk: length, type, data, then CRC32 over type+data.
void pngChunk(std::string &out, const char type[4], const std::string &data)
{
    const auto be32 = [&out](std::uint32_t v) {
        out.push_back(char((v >> 24) & 0xFF));
        out.push_back(char((v >> 16) & 0xFF));
        out.push_back(char((v >> 8) & 0xFF));
        out.push_back(char(v & 0xFF));
    };
    be32(std::uint32_t(data.size()));
    const std::size_t crcStart = out.size();
    out.append(type, 4);
    out += data;
    const auto *from = reinterpret_cast<const Bytef *>(out.data() + crcStart);
    be32(std::uint32_t(crc32(crc32(0L, Z_NULL, 0), from, uInt(4 + data.size()))));
}

}  // namespace

std::string BridgeNode::encodeImagePng(const sensor_msgs::msg::Image &image)
{
    const int w = int(image.width);
    const int h = int(image.height);
    if (w <= 0 || h <= 0)
        return {};

    // 채널 수와 PNG 색 타입. 그 외 형식은 빈 문자열로 돌려보내 호출부가
    // 무슨 형식이었는지 조작자에게 말하게 한다 — 조용히 깨진 파일을 남기는
    // 것보다 낫다.
    int channels = 0;
    char colourType = 0;
    bool swapRb = false;
    if (image.encoding == "rgb8") {
        channels = 3; colourType = 2;
    } else if (image.encoding == "bgr8") {
        channels = 3; colourType = 2; swapRb = true;
    } else if (image.encoding == "mono8") {
        channels = 1; colourType = 0;
    } else {
        return {};
    }

    const std::size_t stride = image.step ? image.step : std::size_t(w) * channels;
    if (image.data.size() < stride * std::size_t(h))
        return {};

    // 필터 0(None) 한 가지만 쓴다. 행마다 최적 필터를 고르면 몇 퍼센트를 더
    // 줄이지만, 촬영은 정지 상태에서 한 장씩 하는 일이라 시간이 아니라
    // 단순함이 낫다.
    std::string raw;
    raw.reserve(std::size_t(h) * (std::size_t(w) * channels + 1));
    for (int y = 0; y < h; ++y) {
        raw.push_back('\0');
        const std::uint8_t *row = image.data.data() + std::size_t(y) * stride;
        if (!swapRb) {
            raw.append(reinterpret_cast<const char *>(row), std::size_t(w) * channels);
        } else {
            for (int x = 0; x < w; ++x) {
                const std::uint8_t *px = row + std::size_t(x) * 3;
                raw.push_back(char(px[2]));
                raw.push_back(char(px[1]));
                raw.push_back(char(px[0]));
            }
        }
    }

    // 지도와 달리 여기서는 Z_BEST_SPEED 를 쓰지 않는다. 이 파일은 NAS 로 가서
    // 점검 근거로 남으므로, 한 번 더 눌러 두는 편이 낫다.
    uLongf bound = compressBound(uLong(raw.size()));
    std::string deflated(bound, '\0');
    if (compress2(reinterpret_cast<Bytef *>(deflated.data()), &bound,
                  reinterpret_cast<const Bytef *>(raw.data()), uLong(raw.size()),
                  Z_DEFAULT_COMPRESSION) != Z_OK)
        return {};
    deflated.resize(bound);

    std::string ihdr;
    for (int v : {w, h}) {
        ihdr.push_back(char((v >> 24) & 0xFF));
        ihdr.push_back(char((v >> 16) & 0xFF));
        ihdr.push_back(char((v >> 8) & 0xFF));
        ihdr.push_back(char(v & 0xFF));
    }
    ihdr.push_back(8);           // bit depth
    ihdr.push_back(colourType);  // 2 = truecolour, 0 = greyscale
    ihdr.append(3, '\0');        // compression, filter, interlace

    std::string png("\x89PNG\r\n\x1a\n", 8);
    pngChunk(png, "IHDR", ihdr);
    pngChunk(png, "IDAT", deflated);
    pngChunk(png, "IEND", {});
    return png;
}

std::string BridgeNode::encodeGridPng(const nav_msgs::msg::OccupancyGrid &grid)
{
    const int w = int(grid.info.width);
    const int h = int(grid.info.height);
    if (w <= 0 || h <= 0 || grid.data.size() < std::size_t(w) * std::size_t(h))
        return {};

    // 8비트 그레이스케일, 필터 0. 데이터가 평평해서 필터를 써도 얻는 게 없다.
    std::string raw;
    raw.reserve(std::size_t(h) * (std::size_t(w) + 1));
    for (int y = h - 1; y >= 0; --y) {     // 행 뒤집기: 격자는 아래에서, 이미지는 위에서 시작
        raw.push_back('\0');
        const std::int8_t *row =
            reinterpret_cast<const std::int8_t *>(grid.data.data()) + std::size_t(y) * std::size_t(w);
        for (int x = 0; x < w; ++x) {
            const int v = row[x];
            // map_server 규약 — 저장한 .pgm 을 불러온 것과 같아 보이게 한다.
            raw.push_back(char(v < 0 ? 205 : (v >= 65 ? 0 : (v <= 25 ? 254 : 205))));
        }
    }

    uLongf bound = compressBound(uLong(raw.size()));
    std::string deflated(bound, '\0');
    if (compress2(reinterpret_cast<Bytef *>(deflated.data()), &bound,
                  reinterpret_cast<const Bytef *>(raw.data()), uLong(raw.size()),
                  Z_BEST_SPEED) != Z_OK)
        return {};
    deflated.resize(bound);

    std::string ihdr;
    for (int v : {w, h}) {
        ihdr.push_back(char((v >> 24) & 0xFF));
        ihdr.push_back(char((v >> 16) & 0xFF));
        ihdr.push_back(char((v >> 8) & 0xFF));
        ihdr.push_back(char(v & 0xFF));
    }
    ihdr.push_back(8);     // bit depth
    ihdr.push_back(0);     // colour type: greyscale
    ihdr.append(3, '\0');  // compression, filter, interlace

    std::string png("\x89PNG\r\n\x1a\n", 8);
    pngChunk(png, "IHDR", ihdr);
    pngChunk(png, "IDAT", deflated);
    pngChunk(png, "IEND", {});
    return png;
}

// ================= 자율주행 =================

void BridgeNode::startNavigation(const Envelope &request)
{
    if (!request.p.contains("x") || !request.p.contains("y")
        || !request.p["x"].is_number() || !request.p["y"].is_number()) {
        respond(request, false, err::kBadPayload, "x, y 가 필요합니다");
        return;
    }

    // 앞선 요청이 아직 Nav2 의 답을 기다리는 중이면 새 요청을 받지 않는다.
    // 둘 다 진행시키면 어느 쪽 응답이 어느 요청의 것인지 알 수 없다.
    if (pendingGoto_) {
        respond(request, false, err::kBusy, "직전 목표를 아직 처리 중입니다");
        return;
    }

    if (!navClient_->action_server_is_ready()) {
        respond(request, false, err::kUnreachable,
                "자율주행이 준비되지 않았습니다 (Nav2 응답 없음)");
        return;
    }

    NavigateToPose::Goal goal;
    goal.pose.header.frame_id = mapFrame_;
    goal.pose.header.stamp = now();
    goal.pose.pose.position.x = request.p["x"].get<double>();
    goal.pose.pose.position.y = request.p["y"].get<double>();

    tf2::Quaternion q;
    q.setRPY(0.0, 0.0, request.p.value("theta", 0.0));
    goal.pose.pose.orientation.x = q.x();
    goal.pose.pose.orientation.y = q.y();
    goal.pose.pose.orientation.z = q.z();
    goal.pose.pose.orientation.w = q.w();

    rclcpp_action::Client<NavigateToPose>::SendGoalOptions opts;

    opts.goal_response_callback = [this](NavGoalHandle::SharedPtr handle) {
        if (!handle) {
            navStatus_ = "rejected";
            settleGoto(false, err::kUnreachable, "자율주행이 목표를 거부했습니다");
            return;
        }
        navGoal_ = handle;
        navGoalId_ = handle->get_goal_id();
        navStatus_ = "navigating";
        settleGoto(true);
    };

    opts.feedback_callback = [this](NavGoalHandle::SharedPtr handle,
                                    const std::shared_ptr<const NavigateToPose::Feedback> fb) {
        if (!handle || handle->get_goal_id() != navGoalId_)
            return;   // a goal we have already replaced
        navDistance_ = fb->distance_remaining;

        // Nav2 leaves estimated_time_remaining at zero with the controllers we
        // run - it is filled in by some plugins and not others. Reporting the
        // zero would put "0 s" on the operator's screen for the whole drive,
        // which reads as "arriving now". Send null instead so the station can
        // show a dash, and only send a number when there is one.
        const double eta = double(fb->estimated_time_remaining.sec)
                           + double(fb->estimated_time_remaining.nanosec) * 1e-9;
        navEta_ = eta > 0.0 ? json(eta) : json(nullptr);
    };

    opts.result_callback = [this](const NavGoalHandle::WrappedResult &result) {
        if (result.goal_id != navGoalId_)
            return;   // the goal this belongs to was preempted; its outcome is moot
        switch (result.code) {
        case rclcpp_action::ResultCode::SUCCEEDED: navStatus_ = "succeeded"; break;
        case rclcpp_action::ResultCode::CANCELED: navStatus_ = "canceled"; break;
        default: navStatus_ = "failed"; break;
        }
        navGoal_.reset();
        navDistance_ = 0.0;
        navEta_ = nullptr;
        // 목표가 끝났다는 사실은 이벤트로도 한 번 보낸다. state/nav 는 손실을
        // 허용하는 스트림이라, 마지막 상태 한 프레임이 떨어지면 관제 화면에
        // 주행이 영영 끝나지 않은 것처럼 남는다.
        sendEnvelope(makeEvent(kChLog, json{{"code", navResultCode(navStatus_)},
                                            {"goal", navGoalPoint_}}));
    };

    navGoalPoint_ = json{{"x", goal.pose.pose.position.x},
                         {"y", goal.pose.pose.position.y},
                         {"theta", request.p.value("theta", 0.0)}};
    navStatus_ = "accepting";
    navDistance_ = 0.0;
    navEta_ = nullptr;
    pendingGoto_ = request;
    pendingGotoAt_ = now();
    navClient_->async_send_goal(goal, opts);
}

void BridgeNode::cancelNavigation(const char *reason)
{
    if (!navGoal_ && !pendingGoto_)
        return;
    if (navGoal_)
        navClient_->async_cancel_goal(navGoal_);
    // 취소를 요청한 쪽이 이유를 알고 있으므로, 아직 답하지 않은 요청은
    // 여기서 닫는다. 그러지 않으면 관제는 목표가 살아 있다고 믿는다.
    settleGoto(false, err::kMode, reason);
    navStatus_ = "canceled";
    RCLCPP_INFO(get_logger(), "자율주행 취소: %s", reason);
}

void BridgeNode::settleGoto(bool ok, const std::string &code, const std::string &message)
{
    if (!pendingGoto_)
        return;
    respond(*pendingGoto_, ok, code, message);
    pendingGoto_.reset();
}

void BridgeNode::publishNav()
{
    if (!server_.isConnected())
        return;

    // Nav2 가 목표 수락 여부를 끝내 답하지 않으면 요청을 닫아 준다. 열어 둔
    // 채 두면 관제는 다음 목표를 보낼 수 없고 (위의 E_BUSY), 이유도 모른다.
    if (pendingGoto_ && (now() - pendingGotoAt_) > rclcpp::Duration(kGoalAcceptTimeout)) {
        navStatus_ = "failed";
        settleGoto(false, err::kUnreachable, "자율주행이 목표에 응답하지 않습니다");
    }

    sendEnvelope(makePublish(kChNav,
                             json{{"status", navStatus_},
                                  {"goal", navGoalPoint_},
                                  {"distance_remaining_m", navDistance_},
                                  {"eta_s", navEta_},
                                  // 경유점 개념은 아직 이 노드에 없다. 필드를
                                  // 빼면 관제가 키 없음과 값 없음을 구분해야
                                  // 하므로 null 로 보낸다.
                                  {"current_waypoint_id", nullptr}}),
                 true);
}

// ================= 안전 =================

void BridgeNode::tickSafety()
{
    // 수동 모드인 동안 제자리 명령을 계속 내보낸다. 두 가지를 한꺼번에 한다 —
    // 로봇을 세워 두고, mux 에서 자율 출력이 선택되지 못하게 한다.
    //
    // 관제가 아니라 여기서 내보내는 이유는 링크다. 관제가 0 을 스트림하게
    // 하면 링크가 끊긴 순간 lease 가 만료되고, 수동 모드인데도 Nav2 가 로봇을
    // 몰기 시작한다. 모드를 아는 것은 이 노드이므로 여기서 잡는다.
    //
    // E-Stop 중에도 내보낸다. 멈추는 것은 안전 게이트가 하지만, 그 사이에
    // 자율 출력이 mux 에서 선택되어 있을 이유는 없다.
    if (manualMode_ || estopEngaged_)
        baseHoldPub_->publish(geometry_msgs::msg::Twist{});
}

bool BridgeNode::estopActive() const
{
    return estopEngaged_ || safetyState_ == "e_stop_latched";
}

void BridgeNode::requestBaseAuthority()
{
    if (!authorityRequestClient_->service_is_ready())
        return;
    auto request = std::make_shared<shalom_interfaces::srv::AuthorityRequest::Request>();
    request->request_id = "hmi-authority-" + std::to_string(++rosRequestSequence_);
    request->requester = "hmi_bridge";
    request->operation = shalom_interfaces::srv::AuthorityRequest::Request::REQUEST_BASE;
    authorityRequestClient_->async_send_request(request);
}

void BridgeNode::requestSafetyResume()
{
    if (!safetyCommandClient_->service_is_ready())
        return;
    auto request = std::make_shared<shalom_interfaces::srv::SafetyCommand::Request>();
    request->request_id = "hmi-safety-" + std::to_string(++rosRequestSequence_);
    request->operator_id = "hmi";
    request->operation = shalom_interfaces::srv::SafetyCommand::Request::RESUME;
    safetyCommandClient_->async_send_request(request);
}

void BridgeNode::publishSafety()
{
    sendEnvelope(makePublish(kChSafety,
                             json{{"estop", estopActive()},
                                  {"mode", manualMode_ ? "manual" : "auto"},
                                  {"state", safetyState_}}));
}

// ================= 텔레메트리 =================

void BridgeNode::publishPose()
{
    if (!server_.isConnected())
        return;

    geometry_msgs::msg::TransformStamped tf;
    try {
        tf = tfBuffer_->lookupTransform(mapFrame_, baseFrame_, tf2::TimePointZero);
    } catch (const tf2::TransformException &) {
        // 변환이 아직 없다. 로그를 매 주기 찍지 않는다 — 기동 직후에는 정상이다.
        return;
    }


    tf2::Quaternion q(tf.transform.rotation.x, tf.transform.rotation.y,
                      tf.transform.rotation.z, tf.transform.rotation.w);
    double roll = 0, pitch = 0, yaw = 0;
    tf2::Matrix3x3(q).getRPY(roll, pitch, yaw);

    // 자세가 얼마나 움직였는지로 속도를 낸다. 한 표본만 보면 값이 튀므로
    // 지수이동평균으로 눌러 준다 — 촬영 허용 판정에 쓰는 값이라, 잠깐의
    // 튐으로 버튼이 깜빡이면 조작자가 못 누른다.
    {
        const rclcpp::Time nowT = now();
        const double x = tf.transform.translation.x;
        const double y = tf.transform.translation.y;
        if (lastPoseAt_.nanoseconds() != 0) {
            const double dt = (nowT - lastPoseAt_).seconds();
            if (dt > 1e-3 && dt < 2.0) {
                const double v = std::hypot(x - lastPoseX_, y - lastPoseY_) / dt;
                double dth = yaw - lastPoseTheta_;
                while (dth > M_PI) dth -= 2 * M_PI;
                while (dth < -M_PI) dth += 2 * M_PI;
                const double w = std::abs(dth) / dt;
                speedLinear_ = 0.7 * speedLinear_ + 0.3 * v;
                speedAngular_ = 0.7 * speedAngular_ + 0.3 * w;
                lastOdomAt_ = nowT;
            }
        }
        lastPoseX_ = x;
        lastPoseY_ = y;
        lastPoseTheta_ = yaw;
        lastPoseAt_ = nowT;
    }

    // 속도를 함께 보낸다. 이것이 없으면 관제는 로봇이 늘 멈춰 있는 줄 알고,
    // 이동 중에도 촬영 버튼이 열려 있다 — 과업지시서 2.2.4 가 금지하는
    // 동적 촬영이 화면에서는 막히지 않는다는 뜻이다.
    const bool odomFresh = lastOdomAt_.nanoseconds() != 0
                           && (now() - lastOdomAt_).seconds() < 1.0;
    sendEnvelope(makePublish(kChPose,
                             json{{"speed", odomFresh ? speedLinear_ : 0.0},
                                  {"yaw_rate", odomFresh ? speedAngular_ : 0.0},
                                  {"moving", isMoving()},
                                  {"x", tf.transform.translation.x},
                                  {"y", tf.transform.translation.y},
                                  {"theta", yaw},
                                  {"frame", mapFrame_}},
                             ++seq_),
                 true);

}

void BridgeNode::markSeen(Sensor &sensor)
{
    const auto stamp = now();
    if (sensor.everSeen) {
        const double dt = (stamp - sensor.lastSeen).seconds();
        if (dt > 1e-6) {
            // 지수이동평균. 한 번의 간격만 보면 값이 심하게 튀어서 조작자가
            // 읽을 수 없다. 0.1 은 대략 최근 10 개 표본을 본다.
            const double hz = 1.0 / dt;
            sensor.measuredHz = sensor.measuredHz > 0.0
                                    ? 0.9 * sensor.measuredHz + 0.1 * hz
                                    : hz;
        }
    }
    sensor.lastSeen = stamp;
    sensor.everSeen = true;
}

/// 관제 포트를 연다. 이미 열려 있으면 아무것도 하지 않는다.
///
/// 실패 사유는 카탈로그의 LINK_PORT_BIND_FAIL 에 해당한다. 그 코드는 사람이
/// 해제해야 사라지는 것으로 분류돼 있어, 여기서도 한 번만 크게 알리고 이후는
/// 눌러서 찍는다 — 5 초마다 같은 줄이 쌓이면 다른 로그가 묻힌다.
bool BridgeNode::openControlPort()
{
    if (server_.isListening())
        return true;

    std::string err;
    // HMI 프로필에는 제어 포트 하나만 저장한다. 상태 확인 포트를 따로
    // 설정하게 하면 둘이 어긋나는 순간 목록이 영원히 빨갛게 보이므로,
    // 바로 다음 포트로 고정한다.
    if (port_ <= 0 || port_ >= 65535) {
        RCLCPP_ERROR(get_logger(), "관제 제어 포트가 올바르지 않습니다: %d", port_);
        return false;
    }
    if (server_.start(std::uint16_t(port_), 0, &err)) {
        RCLCPP_INFO(get_logger(), "관제 연결 대기 중 — TCP %d (제어·상태 확인)", port_);
        return true;
    }

    if (!bindFailed_) {
        bindFailed_ = true;
        RCLCPP_ERROR(get_logger(),
                     "관제 포트 %d 를 열지 못했습니다: %s. 5 초마다 다시 시도합니다 "
                     "(LINK_PORT_BIND_FAIL). 다른 브릿지가 떠 있는지 확인하십시오.",
                     port_, err.c_str());
    } else {
        RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 30000,
                             "관제 포트 %d 아직 열지 못함: %s", port_, err.c_str());
    }
    return false;
}

const char *BridgeNode::postureService(const std::string &posture)
{
    // 이름이 곧 서비스 이름이다. 목록을 여기 두는 이유는 관제가 보낸 문자열을
    // 그대로 서비스 이름으로 쓰지 않기 위해서다 — 그러면 임의의 서비스를
    // 부르게 할 수 있다.
    if (posture == "stand_up" || posture == "stand_down"
        || posture == "balance_stand" || posture == "recovery_stand"
        || posture == "damp")
        return "supported";
    return nullptr;
}

// 선행 조건 때문에 자세 전환을 거절할 때 쓴다. 응답은 명령을 보낸 화면만
// 보지만, 이벤트는 이력에 남는다 — 조작자가 버튼을 눌렀는데 아무 일도
// 일어나지 않은 것처럼 보이는 상황이 기록에 남지 않으면 나중에 짚을 수 없다.
void BridgeNode::refusePosture(const Envelope &request, const std::string &code,
                               const std::string &message)
{
    respond(request, false, code, message);
    sendEnvelope(makeEvent(kChLog, json{{"code", "BASE_POSTURE_BLOCKED"},
                                        {"level", "warn"},
                                        {"msg", message}}));
}

void BridgeNode::handleBasePosture(const Envelope &request)
{
    const std::string posture = request.p.value("posture", std::string());
    if (!postureService(posture)) {
        respond(request, false, err::kBadPayload,
                "알 수 없는 자세: " + posture);
        return;
    }

    if (estopActive()) {
        respond(request, false, err::kEstopEngaged,
                "비상정지 상태에서는 자세를 바꾸지 않습니다");
        return;
    }

    // 팔이 움직이는 중이면 받지 않는다. 팔이 펴진 채 앉으면 차체나 바닥에
    // 부딪힌다. 화면이 버튼을 잠그는 것과 별개로 판정은 로봇이 한다.
    if (motionAuthority_ == "arm_active" || motionAuthority_ == "arm_stopping") {
        refusePosture(request, err::kBusy,
                      "로봇팔이 동작 중입니다. 팔을 멈춘 뒤 다시 시도하십시오");
        return;
    }

    // 움직이는 중에 앉거나 힘을 빼면 넘어진다.
    if (posture == "stand_down" || posture == "damp") {
        // isMoving() 은 오도메트리가 끊긴 것도 "움직인다" 로 본다(정지를
        // 확인할 수 없으니 거절하는 편이 안전하다). 다만 그 이유를 "이동 중"
        // 이라고 말하면, 조작자는 멈춰 서 있는 로봇 앞에서 멈추라는 말을
        // 듣게 되고 다음에 무엇을 해야 하는지 알 수 없다.
        const bool odomStale = lastOdomAt_.nanoseconds() == 0
                               || (now() - lastOdomAt_).seconds() > 1.0;
        if (odomStale) {
            refusePosture(request, err::kHardware,
                          "주행 정보가 들어오지 않아 정지 상태를 확인할 수 없습니다");
            return;
        }
        if (isMoving()) {
            refusePosture(request, err::kMode,
                          "이동 중에는 이 자세로 바꾸지 않습니다. 정지한 뒤 시도하십시오");
            return;
        }
    }

    // damp 은 관절 힘을 빼는 것이라 서 있는 상태에서 누르면 주저앉는다.
    // 실수로 누르는 것을 막기 위해 확인 필드를 요구한다.
    if (posture == "damp" && !request.p.value("confirm", false)) {
        respond(request, false, err::kMode,
                "damp 은 로봇이 주저앉습니다. confirm: true 를 함께 보내십시오");
        return;
    }

    auto client = postureClients_[posture];
    const bool ready = client && client->service_is_ready();

    // 시뮬레이터에는 SportClient 서비스가 없다. 여기서 막아 버리면 관제 화면과
    // 연동 코드를 시뮬레이터에서 시험할 방법이 없으므로, 설정이 켜져 있으면
    // 로그에 남기고 상태만 갱신한다. 로봇이 실제로 움직인 것은 아니라는 사실을
    // 응답과 로그 양쪽에 남긴다.
    if (!ready && postureDryRun_) {
        basePosture_ = posture;
        RCLCPP_WARN(get_logger(), "[모의] 자세 전환 %s — 본체 드라이버 없음", posture.c_str());
        respond(request, true, std::string(), posture + " (모의)");
        publishBase();
        sendEnvelope(makeEvent(kChLog, json{{"code", "BASE_POSTURE_SIMULATED"},
                                            {"level", "warn"},
                                            {"msg", posture}}));
        return;
    }

    if (!ready) {
        const std::string msg = "본체 드라이버가 응답하지 않습니다 (" + posture + ")";
        respond(request, false, err::kHardware, msg);
        sendEnvelope(makeEvent(kChLog, json{{"code", "BASE_POSTURE_FAILED"},
                                            {"level", "error"},
                                            {"msg", msg}}));
        return;
    }

    // 응답은 비동기로 온다. 요청 봉투를 값으로 복사해 두는 이유는, 콜백이
    // 도는 시점에 원본이 이미 사라져 있기 때문이다.
    const Envelope pending = request;
    client->async_send_request(
        std::make_shared<std_srvs::srv::Trigger::Request>(),
        [this, pending, posture](
            rclcpp::Client<std_srvs::srv::Trigger>::SharedFuture future) {
            const auto result = future.get();
            if (result && result->success) {
                basePosture_ = posture;
                respond(pending, true, std::string(), posture);
                publishBase();
                sendEnvelope(makeEvent(kChLog, json{{"code", "BASE_POSTURE_CHANGED"},
                                                    {"level", "info"},
                                                    {"msg", posture}}));
            } else {
                const std::string msg =
                    result ? result->message : std::string("본체가 자세 전환을 거부했습니다");
                respond(pending, false, err::kHardware, msg);
                sendEnvelope(makeEvent(kChLog, json{{"code", "BASE_POSTURE_FAILED"},
                                                    {"level", "error"},
                                                    {"msg", msg}}));
            }
        });
}

void BridgeNode::publishBase()
{
    sendEnvelope(makePublish(kChBase, json{{"posture", basePosture_},
                                           {"motion_authority", motionAuthority_}}));
}

void BridgeNode::publishHealth()
{
    // 관제가 새로 붙으면 로봇이 들고 있던 목록을 한 번 밀어 준다. 예전에는
    // 관제가 설정을 보낼 때만 오갔던 탓에, 갓 접속한 화면에는 점검포인트도
    // 마커도 없는 빈 지도가 떴다 — 로봇은 알고 있는데 화면만 몰랐다.
    const bool connected = server_.isConnected();
    if (connected && !wasConnected_) {
        publishMapCatalog();
        publishActiveMap();
        publishWaypoints();
        publishLocations();
        publishMarkers();
        // 지도도 다시 보낸다. 저장된 지도를 쓰면 map_server 가 한 번만
        // 발행하므로, 이것이 없으면 나중에 붙은 화면은 빈 지도를 본다.
        if (!lastMapPng_.empty())
            sendEnvelope(makePublish(kChMap, lastMapMeta_), false, lastMapPng_);
        // 점검이 도는 중에 관제가 새로 붙을 수 있다. 상태를 안 보내면
        // 화면은 대기로 보고, 버튼이 "자율주행 시작" 인 채로 남는다.
        publishMission();
        // 자세도 바뀔 때만 보내는 채널이라 같은 문제가 있다. 없으면 갓 붙은
        // 화면의 자세 표시가 비어 있고, 조작자는 로봇이 서 있는지 앉아
        // 있는지를 화면에서 알 수 없다.
        publishBase();
    }
    wasConnected_ = connected;

    if (!connected)
        return;

    json list = json::array();
    for (const auto &sensor : sensors_) {
        const double age = (now() - sensor.lastSeen).seconds();
        // 기대 주기의 세 배를 놓치면 끊긴 것으로 본다. 한 번 놓친 것으로
        // 경고하면 조작자가 경고를 무시하는 법부터 배운다.
        const double stale = sensor.expectedHz > 0.0 ? 3.0 / sensor.expectedHz : 3.0;

        std::string state = "ok";
        std::string detail;
        if (!sensor.everSeen) {
            state = "lost";
            detail = "신호 없음";
        } else if (age > stale) {
            state = "lost";
            detail = "끊김";
        } else if (sensor.expectedHz > 0.0 && sensor.measuredHz < sensor.expectedHz * 0.6) {
            // 오고는 있는데 느리다. 조작자에게는 끊긴 것과 다른 상황이다 —
            // 화면은 나오는데 프레임이 흘러내리는 카메라가 여기에 해당한다.
            state = "degraded";
            detail = "주기 저하";
        }

        list.push_back(json{{"id", sensor.id},
                            {"name", sensor.name},
                            {"expected_hz", sensor.expectedHz},
                            {"actual_hz", sensor.everSeen ? sensor.measuredHz : 0.0},
                            {"last_seen_ms", sensor.everSeen ? json(int(age * 1000.0))
                                                             : json(nullptr)},
                            {"state", state},
                            {"detail", detail}});
    }

    json link{{"rx_bytes_per_s", server_.rxBytes()},
              {"tx_bytes_per_s", server_.txBytes()}};
    server_.resetByteCounters();

    sendEnvelope(makePublish(kChHealth, json{{"sensors", list}, {"link", link}}), true);
}

}  // namespace hmi_bridge
