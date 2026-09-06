#include "shalom_bridge/bridge_node.hpp"

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
constexpr auto kChLog = "evt/log";

constexpr auto kCmdVel = "cmd/cmd_vel";
constexpr auto kCmdEstop = "cmd/estop";
constexpr auto kCmdEstopRelease = "cmd/estop_release";
constexpr auto kCmdMode = "cmd/mode";
constexpr auto kCmdGoto = "cmd/goto";

}  // namespace

BridgeNode::BridgeNode() : rclcpp::Node("shalom_bridge")
{
    port_ = int(declare_parameter("port", port_));
    mapFrame_ = declare_parameter("map_frame", mapFrame_);
    baseFrame_ = declare_parameter("base_frame", baseFrame_);
    deadman_ = std::chrono::milliseconds(
        declare_parameter("deadman_ms", int(deadman_.count())));
    heartbeatTimeout_ = std::chrono::milliseconds(
        declare_parameter("heartbeat_timeout_ms", int(heartbeatTimeout_.count())));

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
            // TODO(integration): PNG 로 인코딩해 바이너리 프레임으로 보낸다.
            // 인코딩 시 행 순서를 뒤집어야 한다 (프로토콜 §2.2).
            // 지금은 메타데이터만 보내 관제가 맵 크기를 알 수 있게 한다.
            sendEnvelope(makePublish(kChMap,
                                     json{{"width", msg->info.width},
                                          {"height", msg->info.height},
                                          {"resolution", msg->info.resolution},
                                          {"origin",
                                           json{{"x", msg->info.origin.position.x},
                                                {"y", msg->info.origin.position.y},
                                                {"theta", 0.0}}},
                                          {"encoding", "none"}}));
            RCLCPP_INFO(get_logger(), "맵 수신: %ux%u", msg->info.width,
                        msg->info.height);
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

void BridgeNode::handleFrame(const shalom::Frame &frame)
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

void BridgeNode::sendEnvelope(const Envelope &env, bool lossy)
{
    server_.send(shalom::encodeFrame(env.toHeader()), lossy);
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
        // TODO(integration): Nav2 액션 취소 호출.
        respond(request, true);
        return;
    }

    if (request.ch == kCmdGoto) {
        if (manualMode_) {
            respond(request, false, err::kMode, "수동 모드에서는 자율 이동을 실행하지 않습니다");
            return;
        }
        // TODO(integration): nav2_msgs::action::NavigateToPose 로 목표 전송.
        respond(request, true);
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
    pendingTwist_.linear.x = payload.value("vx", 0.0);
    pendingTwist_.linear.y = payload.value("vy", 0.0);
    pendingTwist_.angular.z = payload.value("wz", 0.0);
    haveJogCommand_ = true;
    lastCmdVel_ = now();
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

void BridgeNode::publishHealth()
{
    if (!server_.isConnected())
        return;

    // TODO(integration): 각 센서의 실측 주기를 측정해 채운다. 끊김 판정은
    // 여기서 수행해 state 로 내려준다 — 관제는 표시만 한다 (프로토콜 §9.2).
    json link{{"rx_bytes_per_s", server_.rxBytes()},
              {"tx_bytes_per_s", server_.txBytes()}};
    server_.resetByteCounters();

    sendEnvelope(makePublish(kChHealth, json{{"sensors", json::array()}, {"link", link}}),
                 true);
}

}  // namespace shalom_bridge
