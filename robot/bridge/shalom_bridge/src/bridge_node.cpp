#include "shalom_bridge/bridge_node.hpp"

#include <tf2/LinearMath/Quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#include <fstream>
#include <string>

// 점유격자를 PNG 로 눌러 관제에 보낸다 (encodeGridPng).
#include <zlib.h>

#include <tf2/LinearMath/Matrix3x3.h>
#include <tf2/LinearMath/Quaternion.h>

#include <cmath>

namespace shalom_bridge {
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

/// Nav2 has this long to say whether it accepts a goal before the station's
/// request is answered with a failure. Goal acceptance is a planner-side
/// decision that normally lands in milliseconds; two seconds means it is wedged.
constexpr std::chrono::seconds kGoalAcceptTimeout{2};
constexpr auto kChLog = "evt/log";

constexpr auto kCmdVel = "cmd/cmd_vel";
constexpr auto kCmdEstop = "cmd/estop";
constexpr auto kCmdEstopRelease = "cmd/estop_release";
constexpr auto kCmdMode = "cmd/mode";
constexpr auto kCmdGoto = "cmd/goto";
constexpr auto kCmdNavCancel = "cmd/nav_cancel";
constexpr auto kCmdWaypointsSet = "cmd/waypoints/set";
constexpr auto kCmdLocationsSet = "cmd/locations/set";
constexpr auto kCmdMarkersSet = "cmd/markers/set";
constexpr auto kCmdVideoQuality = "cmd/video/quality";
constexpr auto kCmdMissionStart = "cmd/mission/start";
constexpr auto kCmdMissionPause = "cmd/mission/pause";
constexpr auto kCmdMissionResume = "cmd/mission/resume";
constexpr auto kCmdMissionStop = "cmd/mission/stop";
constexpr auto kCmdPowerPolicy = "cmd/power/policy";
constexpr auto kCmdArmPreset = "cmd/arm/preset";
constexpr auto kCmdArmJointGoal = "cmd/arm/joint_goal";
constexpr auto kCmdArmEeGoal = "cmd/arm/ee_goal";
constexpr auto kCmdArmStop = "cmd/arm/stop";

/// FR3 joint names, in the order the arm reports them.
const std::vector<std::string> kArmJointNames{
    "fr3_shoulder", "fr3_upperarm", "fr3_forearm",
    "fr3_wrist1", "fr3_wrist2", "fr3_wrist3"};

/// 트레일을 이만큼 움직였을 때만 한 점을 남긴다. 서 있는 로봇이 초당 두 점씩
/// 같은 자리를 쌓으면 화면의 선이 뭉치고 대역폭만 먹는다.
constexpr double kTrailMinStepM = 0.05;

}  // namespace

BridgeNode::BridgeNode() : rclcpp::Node("shalom_bridge")
{
    port_ = int(declare_parameter("port", port_));
    // 영상 노드의 완전한 이름. 카메라를 두 대 달면 역할별로 갈리므로 설정으로 둔다.
    videoNodeName_ = declare_parameter("video_node",
                                       std::string("/fr3/camera/video_streamer"));
    mapFrame_ = declare_parameter("map_frame", mapFrame_);
    baseFrame_ = declare_parameter("base_frame", baseFrame_);
    deadman_ = std::chrono::milliseconds(
        declare_parameter("deadman_ms", int(deadman_.count())));
    heartbeatTimeout_ = std::chrono::milliseconds(
        declare_parameter("heartbeat_timeout_ms", int(heartbeatTimeout_.count())));
    maxLinVelX_ = declare_parameter("max_lin_vel_x", maxLinVelX_);
    maxLinVelY_ = declare_parameter("max_lin_vel_y", maxLinVelY_);
    maxAngVelZ_ = declare_parameter("max_ang_vel_z", maxAngVelZ_);

    lastHeartbeat_ = now();
    lastCmdVel_ = now();

    cmdVelPub_ = create_publisher<geometry_msgs::msg::Twist>("cmd_vel", 10);

    // 안전 노드가 지켜보는 생존 신호. 이 노드가 죽으면 발행이 멈추고,
    // 안전 노드가 그것을 근거로 로봇을 정지시킨다. 정지 판단을 여기에 두면
    // 이 노드의 크래시가 곧 감시자 없는 주행이 된다.
    linkAlivePub_ = create_publisher<std_msgs::msg::Bool>("~/link_alive", 10);
    estopPub_ = create_publisher<std_msgs::msg::Bool>("~/estop_request", 10);

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

    std::string err;
    if (!server_.start(std::uint16_t(port_), &err)) {
        RCLCPP_FATAL(get_logger(), "관제 포트 %d 를 열지 못했습니다: %s", port_,
                     err.c_str());
        throw std::runtime_error("failed to open control port");
    }
    RCLCPP_INFO(get_logger(), "관제 연결 대기 중 — 포트 %d", port_);

    using namespace std::chrono_literals;
    linkTimer_ = create_wall_timer(10ms, [this] { pollLink(); });
    poseTimer_ = create_wall_timer(100ms, [this] { publishPose(); });     // 10 Hz
    safetyTimer_ = create_wall_timer(50ms, [this] { tickSafety(); });     // 20 Hz
    healthTimer_ = create_wall_timer(1s, [this] { publishHealth(); });
    navTimer_ = create_wall_timer(200ms, [this] { publishNav(); });       // 5 Hz
    systemTimer_ = create_wall_timer(1s, [this] { publishSystem(); });
    trailTimer_ = create_wall_timer(500ms, [this] { publishTrail(); });   // 2 Hz

    // 팔은 이름으로 지시한다. 받는 쪽(시뮬레이터든 실기 드라이버든)이 자기
    // 순서를 쓰므로, 색인으로 보내면 언젠가 다른 축이 움직인다.
    armCmdPub_ = create_publisher<sensor_msgs::msg::JointState>("fr3/joint_command", 10);

    // 액션 이름은 Nav2 기본값이다. 다른 이름을 쓰는 스택에 붙일 때는 런치에서
    // 리매핑한다 — 여기에 파라미터를 하나 더 만들면 프로토콜 문서와 어긋날
    // 자리가 하나 더 생긴다.
    navClient_ = rclcpp_action::create_client<NavigateToPose>(this, "navigate_to_pose");

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
        lastHeartbeat_ = now();
    }
    if (events.protocolError) {
        // 프레이밍이 어긋났다는 것은 링크나 양쪽 구현 중 하나에 문제가
        // 있다는 뜻이다. 조용히 넘기면 안 된다.
        RCLCPP_ERROR(get_logger(), "프로토콜 오류로 연결을 끊었습니다: %s",
                     events.protocolErrorDetail.c_str());
        // 이미 끊긴 뒤라 이 이벤트는 다음 연결에서야 전달된다. 그래도 보내는
        // 이유는, 재연결한 관제가 직전에 무슨 일이 있었는지 알아야 하기 때문이다.
        sendEnvelope(makeEvent(kChLog, json{{"code", "FRAME_BAD_MAGIC"},
                                            {"level", "error"},
                                            {"msg", events.protocolErrorDetail}}));
    }
    if (events.clientDisconnected) {
        RCLCPP_WARN(get_logger(), "관제 연결 끊김");
        // 조작 명령을 즉시 폐기한다. 데드맨을 기다리지 않는다.
        haveJogCommand_ = false;
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

    if (env->t == mtype::kHb)
        handleHeartbeat(*env);
    else if (env->t == mtype::kReq)
        handleRequest(*env);
    else if (env->t == mtype::kPub && env->ch == kCmdVel)
        applyCmdVel(env->p);
    else if (env->t == mtype::kSub)
        RCLCPP_INFO(get_logger(), "구독 요청 수신");
}

void BridgeNode::handleHeartbeat(const Envelope &heartbeat)
{
    lastHeartbeat_ = now();
    // 같은 seq 를 돌려보내 관제가 왕복 지연을 잴 수 있게 한다.
    sendEnvelope(makeHeartbeat(heartbeat.p.value("seq", std::int64_t{0})));
}

void BridgeNode::sendEnvelope(const Envelope &env, bool lossy, const std::string &payload)
{
    server_.send(inspection::encodeFrame(env.toHeader(), payload), lossy);
}

void BridgeNode::respond(const Envelope &request, bool ok, const std::string &code,
                         const std::string &message)
{
    sendEnvelope(makeResponse(request, ok, code, message));
}

// ================= 명령 =================

bool BridgeNode::commandsAllowed(const Envelope &request)
{
    if (estopEngaged_ && request.ch != kCmdEstopRelease && request.ch != kCmdEstop) {
        respond(request, false, err::kEstopEngaged, "E-Stop 발동 상태입니다");
        return false;
    }
    return true;
}

void BridgeNode::handleRequest(const Envelope &request)
{
    if (!commandsAllowed(request))
        return;

    if (request.ch == kCmdEstop) {
        // 발동에는 어떤 조건도 걸지 않는다. 실제 정지는 안전 노드가 수행한다.
        estopEngaged_ = true;
        std_msgs::msg::Bool msg;
        msg.data = true;
        estopPub_->publish(msg);
        haveJogCommand_ = false;
        // 정지는 안전 노드가 시킨다. 여기서 취소하는 것은 해제 뒤에 Nav2 가
        // 아무도 다시 누르지 않은 목표로 출발하는 것을 막기 위해서다.
        // 순회 중이었으면 멈춘 자리를 기억한다. idle 로 되돌리면 해제한 뒤
        // 처음부터 다시 돌아야 하고, 화면에는 재개 버튼이 뜨지 않는다.
        if (mission_ == Mission::Running)
            pauseMission("E-Stop");
        else
            cancelNavigation("E-Stop 발동");
        respond(request, true);
        RCLCPP_WARN(get_logger(), "E-Stop 발동 (관제 요청)");
        return;
    }

    if (request.ch == kCmdEstopRelease) {
        // 해제 권한 확인은 관제가 수행한다. 여기서는 상태만 되돌린다.
        estopEngaged_ = false;
        std_msgs::msg::Bool msg;
        msg.data = false;
        estopPub_->publish(msg);
        respond(request, true);
        RCLCPP_WARN(get_logger(), "E-Stop 해제 (관제 요청)");
        return;
    }

    if (request.ch == kCmdMode) {
        manualMode_ = request.p.value("mode", std::string("auto")) == "manual";
        // 수동 전환 시 자율주행을 즉시 중단한다 (지시서 2.2.5).
        if (manualMode_)
            cancelNavigation("수동 모드 전환");
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
        waypoints_ = request.p.value("points", json::array());
        respond(request, true);
        publishWaypoints();
        RCLCPP_INFO(get_logger(), "점검 지점 %zu 개 수신", waypoints_.size());
        return;
    }

    if (request.ch == kCmdLocationsSet) {
        locations_ = request.p.value("locations", json::array());
        respond(request, true);
        publishLocations();
        return;
    }

    if (request.ch == kCmdMarkersSet) {
        // 측량해 넣은 마커 자리다. 관제에서 통째로 갈아 끼우고, 로봇은 그것을
        // 그대로 들고 있다가 되돌려 준다 — 어느 태그가 어디 붙어 있는지는
        // 사람이 재어 오는 값이라 로봇이 스스로 정할 수 있는 것이 아니다.
        markers_ = request.p.value("markers", json::array());
        respond(request, true);
        publishMarkers();
        RCLCPP_INFO(get_logger(), "마커 %zu개 등록", markers_.size());
        return;
    }

    if (request.ch == kCmdVideoQuality) {
        const std::string want = request.p.value("preset", std::string());
        if (want != "high" && want != "low" && want != "saver") {
            respond(request, false, err::kBadPayload,
                    "화질은 high, low, saver 중 하나여야 합니다");
            return;
        }
        if (!videoParams_) {
            videoParams_ = std::make_shared<rclcpp::AsyncParametersClient>(
                this, videoNodeName_);
        }
        if (!videoParams_->service_is_ready()) {
            respond(request, false, err::kUnreachable, "영상 노드가 응답하지 않습니다");
            return;
        }
        videoParams_->set_parameters({rclcpp::Parameter("quality", want)});
        videoQuality_ = want;
        respond(request, true);
        RCLCPP_INFO(get_logger(), "화질 %s 로 변경 요청", want.c_str());
        return;
    }

    if (request.ch == kCmdMissionStart) {
        if (mission_ == Mission::Running) {
            respond(request, false, err::kBusy, "이미 점검 중입니다");
            return;
        }
        if (waypoints_.empty()) {
            respond(request, false, err::kBadPayload, "점검포인트가 없습니다");
            return;
        }
        if (estopEngaged_) {
            respond(request, false, err::kMode,
                    "비상정지 상태입니다. 해제한 뒤 시작하십시오");
            return;
        }
        respond(request, true);
        startMission();
        return;
    }

    if (request.ch == kCmdMissionPause) {
        if (mission_ != Mission::Running) {
            respond(request, false, err::kMode, "점검 중이 아닙니다");
            return;
        }
        respond(request, true);
        pauseMission("관제 요청");
        return;
    }

    if (request.ch == kCmdMissionResume) {
        if (mission_ != Mission::Paused) {
            respond(request, false, err::kMode, "일시정지 상태가 아닙니다");
            return;
        }
        if (estopEngaged_) {
            respond(request, false, err::kMode,
                    "비상정지 상태입니다. 해제한 뒤 재개하십시오");
            return;
        }
        respond(request, true);
        resumeMission();
        return;
    }

    if (request.ch == kCmdMissionStop) {
        if (mission_ == Mission::Idle) {
            respond(request, false, err::kMode, "점검 중이 아닙니다");
            return;
        }
        respond(request, true);
        stopMission("관제 요청");
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
        startNavigation(request);
        return;
    }

    // 미지원 채널은 조용히 무시하지 않고 사유를 돌려준다. 관제가 새 기능을
    // 쓰려다 아무 반응이 없으면 원인을 짚을 수 없다.
    respond(request, false, err::kUnknownChannel, "지원하지 않는 채널: " + request.ch);
}

void BridgeNode::applyCmdVel(const json &payload)
{
    if (estopEngaged_ || !manualMode_)
        return;

    const auto clamp = [](double v, double lim) { return std::max(-lim, std::min(v, lim)); };
    const double vx = payload.value("vx", 0.0);
    const double vy = payload.value("vy", 0.0);
    const double wz = payload.value("wz", 0.0);

    pendingTwist_.linear.x = clamp(vx, maxLinVelX_);
    pendingTwist_.linear.y = clamp(vy, maxLinVelY_);
    pendingTwist_.angular.z = clamp(wz, maxAngVelZ_);

    // 잘라냈다는 사실은 알려야 한다. 조용히 줄이면 관제는 자기가 보낸 대로
    // 가고 있다고 믿고, 로봇이 왜 느린지 아무도 모른다. 조작 중에는 이 명령이
    // 초당 수십 번 오므로 매번 찍지 않고, 잘릴 때만 1 초에 한 번 찍는다.
    if (std::abs(vx) > maxLinVelX_ || std::abs(vy) > maxLinVelY_
        || std::abs(wz) > maxAngVelZ_) {
        RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 1000,
                             "조작 명령이 한도를 넘어 잘렸다: "
                             "요청 %.2f/%.2f/%.2f, 한도 %.2f/%.2f/%.2f",
                             vx, vy, wz, maxLinVelX_, maxLinVelY_, maxAngVelZ_);
    }

    haveJogCommand_ = true;
    lastCmdVel_ = now();
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
                                  {"net_rssi", nullptr}}),
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

const char *BridgeNode::missionStateName() const
{
    switch (mission_) {
    case Mission::Running: return "running";
    case Mission::Paused:  return "paused";
    case Mission::Idle:    break;
    }
    return "idle";
}

void BridgeNode::publishMission()
{
    sendEnvelope(makePublish(kChMission,
                             json{{"state", missionStateName()},
                                  {"index", missionIndex_},
                                  {"total", waypoints_.size()}}));
}

void BridgeNode::setWaypointStatus(std::size_t index, const char *status)
{
    if (index >= waypoints_.size())
        return;
    waypoints_[index]["status"] = status;
    publishWaypoints();
}

bool BridgeNode::navigateToWaypoint(std::size_t index)
{
    if (index >= waypoints_.size())
        return false;

    const auto &wp = waypoints_[index];
    if (!wp.contains("x") || !wp.contains("y")) {
        RCLCPP_ERROR(get_logger(), "점검포인트 %zu 에 좌표가 없다", index);
        return false;
    }
    if (!navClient_->action_server_is_ready()) {
        RCLCPP_ERROR(get_logger(), "자율주행이 준비되지 않았다 (Nav2 응답 없음)");
        return false;
    }

    NavigateToPose::Goal goal;
    goal.pose.header.frame_id = mapFrame_;
    goal.pose.header.stamp = now();
    goal.pose.pose.position.x = wp["x"].get<double>();
    goal.pose.pose.position.y = wp["y"].get<double>();

    tf2::Quaternion q;
    q.setRPY(0.0, 0.0, wp.value("theta", 0.0));
    goal.pose.pose.orientation.x = q.x();
    goal.pose.pose.orientation.y = q.y();
    goal.pose.pose.orientation.z = q.z();
    goal.pose.pose.orientation.w = q.w();

    rclcpp_action::Client<NavigateToPose>::SendGoalOptions opts;
    opts.goal_response_callback = [this](NavGoalHandle::SharedPtr handle) {
        if (!handle) {
            navStatus_ = "rejected";
            RCLCPP_ERROR(get_logger(), "자율주행이 점검 목표를 거부했다");
            pauseMission("목표 거부");
            return;
        }
        navGoal_ = handle;
        navGoalId_ = handle->get_goal_id();
        navStatus_ = "navigating";
    };
    opts.feedback_callback = [this](NavGoalHandle::SharedPtr handle,
                                    const std::shared_ptr<const NavigateToPose::Feedback> fb) {
        if (!handle || handle->get_goal_id() != navGoalId_)
            return;
        navDistance_ = fb->distance_remaining;
        const double eta = double(fb->estimated_time_remaining.sec)
                           + double(fb->estimated_time_remaining.nanosec) * 1e-9;
        navEta_ = eta > 0.0 ? json(eta) : json(nullptr);
    };
    opts.result_callback = [this](const NavGoalHandle::WrappedResult &result) {
        if (result.goal_id != navGoalId_)
            return;
        const bool ok = result.code == rclcpp_action::ResultCode::SUCCEEDED;
        const bool canceled = result.code == rclcpp_action::ResultCode::CANCELED;
        navStatus_ = ok ? "succeeded" : canceled ? "canceled" : "failed";
        navGoal_.reset();
        navDistance_ = 0.0;
        navEta_ = nullptr;
        sendEnvelope(makeEvent(kChLog, json{{"code", "NAV_" + navStatus_},
                                            {"goal", navGoalPoint_}}));
        onMissionGoalFinished(ok, canceled);
    };

    navGoalPoint_ = json{{"x", goal.pose.pose.position.x},
                         {"y", goal.pose.pose.position.y},
                         {"theta", wp.value("theta", 0.0)}};
    navStatus_ = "accepting";
    navDistance_ = 0.0;
    navEta_ = nullptr;
    missionOwnsGoal_ = true;
    navClient_->async_send_goal(goal, opts);
    setWaypointStatus(index, "current");
    return true;
}

void BridgeNode::startMission()
{
    // 다시 시작하는 것이므로 이전 결과를 지운다. 남겨 두면 완료 표시가
    // 그대로 있는 채로 로봇만 처음부터 도는 그림이 된다.
    for (std::size_t i = 0; i < waypoints_.size(); ++i)
        waypoints_[i]["status"] = "todo";
    publishWaypoints();

    missionIndex_ = 0;
    mission_ = Mission::Running;
    publishMission();
    RCLCPP_INFO(get_logger(), "점검 시작 — 포인트 %zu 개", waypoints_.size());

    if (!navigateToWaypoint(missionIndex_))
        pauseMission("첫 목표를 보내지 못했다");
}

void BridgeNode::pauseMission(const char *why)
{
    if (mission_ == Mission::Idle)
        return;
    mission_ = Mission::Paused;
    // 목표를 물린 채 두면 해제한 뒤 로봇이 아무도 다시 누르지 않은 자리로
    // 출발한다. 자리는 missionIndex_ 가 들고 있으므로 잃는 것은 없다.
    if (missionOwnsGoal_)
        cancelNavigation(why);
    publishMission();
    RCLCPP_WARN(get_logger(), "점검 일시정지 (%s) — 포인트 %zu 에서", why,
                missionIndex_ + 1);
}

void BridgeNode::resumeMission()
{
    mission_ = Mission::Running;
    publishMission();
    RCLCPP_INFO(get_logger(), "점검 재개 — 포인트 %zu 부터", missionIndex_ + 1);
    if (!navigateToWaypoint(missionIndex_))
        pauseMission("목표를 다시 보내지 못했다");
}

void BridgeNode::stopMission(const char *why)
{
    const bool wasRunning = mission_ != Mission::Idle;
    mission_ = Mission::Idle;
    missionIndex_ = 0;
    if (missionOwnsGoal_)
        cancelNavigation(why);
    missionOwnsGoal_ = false;

    // 취소한 순회의 진행 표시는 지운다. 어디까지 갔는지는 이력에 남고,
    // 화면에 남겨 두면 다음 시작 때 완료된 것처럼 보인다.
    for (std::size_t i = 0; i < waypoints_.size(); ++i)
        waypoints_[i]["status"] = "todo";
    publishWaypoints();
    publishMission();
    if (wasRunning)
        RCLCPP_WARN(get_logger(), "점검 취소 (%s)", why);
}

void BridgeNode::onMissionGoalFinished(bool succeeded, bool canceled)
{
    if (!missionOwnsGoal_)
        return;
    missionOwnsGoal_ = false;

    // 우리가 세운 것이다. 일시정지·취소가 이미 상태를 정했으므로 여기서
    // 한 칸 넘기면 안 된다.
    if (canceled || mission_ != Mission::Running)
        return;

    if (!succeeded) {
        setWaypointStatus(missionIndex_, "error");
        pauseMission("목표에 도달하지 못했다");
        return;
    }

    setWaypointStatus(missionIndex_, "done");
    ++missionIndex_;

    if (missionIndex_ >= waypoints_.size()) {
        mission_ = Mission::Idle;
        missionIndex_ = 0;
        publishMission();
        sendEnvelope(makeEvent(kChLog, json{{"code", "MISSION_DONE"}}));
        RCLCPP_INFO(get_logger(), "점검 완료");
        return;
    }

    if (!navigateToWaypoint(missionIndex_))
        pauseMission("다음 목표를 보내지 못했다");
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
        sendEnvelope(makeEvent(kChLog, json{{"code", "NAV_" + navStatus_},
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
    const auto elapsedMs = [this](const rclcpp::Time &since) {
        return (now() - since).nanoseconds() / 1000000;
    };

    // 데드맨: 조작 명령이 끊기면 즉시 0 을 발행한다. 관제가 멈추거나 링크가
    // 끊겨도 로봇이 계속 달리지 않게 하는 마지막 방어선이다.
    if (haveJogCommand_ && elapsedMs(lastCmdVel_) > deadman_.count()) {
        haveJogCommand_ = false;
        pendingTwist_ = geometry_msgs::msg::Twist{};
        cmdVelPub_->publish(pendingTwist_);
    } else if (haveJogCommand_) {
        cmdVelPub_->publish(pendingTwist_);
    }

    // 생존 신호. 관제 하트비트가 신선한 동안에만 발행한다.
    // 이 노드가 죽으면 발행 자체가 멈추고, 안전 노드가 그것을 정지 근거로 쓴다.
    const bool alive = server_.isConnected()
                       && elapsedMs(lastHeartbeat_) <= heartbeatTimeout_.count();
    std_msgs::msg::Bool msg;
    msg.data = alive;
    linkAlivePub_->publish(msg);
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

    sendEnvelope(makePublish(kChPose,
                             json{{"x", tf.transform.translation.x},
                                  {"y", tf.transform.translation.y},
                                  {"theta", yaw},
                                  {"frame", mapFrame_}},
                             ++seq_),
                 true);

    sendEnvelope(makePublish(kChSafety, json{{"estop", estopEngaged_},
                                             {"mode", manualMode_ ? "manual" : "auto"}}));
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

void BridgeNode::publishHealth()
{
    // 관제가 새로 붙으면 로봇이 들고 있던 목록을 한 번 밀어 준다. 예전에는
    // 관제가 설정을 보낼 때만 오갔던 탓에, 갓 접속한 화면에는 점검포인트도
    // 마커도 없는 빈 지도가 떴다 — 로봇은 알고 있는데 화면만 몰랐다.
    const bool connected = server_.isConnected();
    if (connected && !wasConnected_) {
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

}  // namespace shalom_bridge
