// Copyright (c) 2026 WeGo Robotics. All rights reserved.

// Dedicated HMI E-Stop ingress.
//
// This process deliberately owns a different TCP socket from hmi_bridge. The
// safety manager receives its only external heartbeat from here, so a working
// map/telemetry connection cannot hide a failed E-Stop path.

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>

#include <rclcpp/rclcpp.hpp>

#include <shalom_interfaces/msg/safety_heartbeat.hpp>
#include <shalom_interfaces/srv/safety_command.hpp>

#include "gateway_transport/envelope.hpp"
#include "gateway_transport/tcp_server.hpp"

namespace {

using SafetyCommand = shalom_interfaces::srv::SafetyCommand;
using SafetyHeartbeat = shalom_interfaces::msg::SafetyHeartbeat;
using gateway_transport::Envelope;
using gateway_transport::LinkEvents;
using gateway_transport::TcpServer;

constexpr auto kCmdEstop = "cmd/estop";
constexpr auto kCmdEstopRelease = "cmd/estop_release";

class EstopBridgeNode final : public rclcpp::Node {
public:
    EstopBridgeNode() : Node("estop_bridge")
    {
        port_ = declare_parameter("port", port_);
        robotId_ = declare_parameter("robot_id", robotId_);
        heartbeatTimeout_ = std::chrono::milliseconds(declare_parameter(
            "heartbeat_timeout_ms", int(heartbeatTimeout_.count())));
        if (robotId_.empty()) {
            throw std::invalid_argument("robot_id must be configured from robot metadata");
        }
        if (port_ <= 0 || port_ >= 65535)
            throw std::invalid_argument("estop_bridge port must be 1..65534");

        safetyCommandClient_ = create_client<SafetyCommand>("/safety/command");
        heartbeatPub_ = create_publisher<SafetyHeartbeat>("/safety/heartbeat", 10);

        using namespace std::chrono_literals;
        if (!openPort()) {
            bindRetryTimer_ = create_wall_timer(5s, [this] {
                if (openPort())
                    bindRetryTimer_->cancel();
            });
        }
        linkTimer_ = create_wall_timer(10ms, [this] { pollLink(); });
        heartbeatTimer_ = create_wall_timer(50ms, [this] { publishHeartbeat(); });
    }

    ~EstopBridgeNode() override { server_.stop(); }

private:
    bool openPort()
    {
        if (server_.isListening())
            return true;

        std::string error;
        if (server_.start(static_cast<std::uint16_t>(port_), 0, &error)) {
            bindFailed_ = false;
            RCLCPP_INFO(get_logger(),
                        "E-Stop 전용 연결 대기 중 — TCP %d (heartbeat %ld ms)",
                        port_, heartbeatTimeout_.count());
            return true;
        }

        if (!bindFailed_) {
            bindFailed_ = true;
            RCLCPP_ERROR(get_logger(),
                         "E-Stop TCP 포트 %d를 열지 못했습니다: %s. 5초마다 재시도합니다",
                         port_, error.c_str());
        } else {
            RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 30000,
                                 "E-Stop TCP 포트 %d를 아직 열지 못함: %s",
                                 port_, error.c_str());
        }
        return false;
    }

    void pollLink()
    {
        const LinkEvents events = server_.drain();
        if (events.empty())
            return;

        if (events.clientConnected) {
            receivedHeartbeat_ = false;
            RCLCPP_INFO(get_logger(), "E-Stop 제어기 연결됨");
        }
        if (events.clientDisconnected) {
            receivedHeartbeat_ = false;
            RCLCPP_WARN(get_logger(), "E-Stop 제어기 연결 끊김");
            publishHeartbeat();  // 다음 50 ms를 기다리지 않고 안전 정지로 전환.
        }
        if (events.protocolError) {
            receivedHeartbeat_ = false;
            RCLCPP_ERROR(get_logger(), "E-Stop 프로토콜 오류: %s",
                         events.protocolErrorDetail.c_str());
            publishHeartbeat();
        }
        for (const auto &frame : events.frames)
            handleFrame(frame);
    }

    void handleFrame(const inspection::Frame &frame)
    {
        std::string error;
        const auto envelope = Envelope::fromHeader(frame.header, &error);
        if (!envelope) {
            RCLCPP_WARN(get_logger(), "E-Stop 봉투 해석 실패: %s", error.c_str());
            return;
        }
        if (!envelope->robot.empty() && envelope->robot != robotId_) {
            if (envelope->t == gateway_transport::mtype::kReq)
                respond(*envelope, false, gateway_transport::err::kRobotMismatch,
                        "이 로봇은 " + robotId_ + " 입니다");
            return;
        }

        if (envelope->t == gateway_transport::mtype::kHb) {
            receivedHeartbeat_ = true;
            lastHeartbeat_ = std::chrono::steady_clock::now();
            sendEnvelope(gateway_transport::makeHeartbeat(
                envelope->p.value("seq", std::int64_t{0})));
            return;
        }
        if (envelope->t != gateway_transport::mtype::kReq)
            return;

        if (envelope->ch == kCmdEstop) {
            forwardCommand(*envelope, SafetyCommand::Request::ENGAGE_SOFTWARE_ESTOP);
        } else if (envelope->ch == kCmdEstopRelease) {
            forwardCommand(*envelope, SafetyCommand::Request::RELEASE_SOFTWARE_ESTOP);
        } else {
            respond(*envelope, false, gateway_transport::err::kUnknownChannel,
                    "E-Stop 전용 포트에서는 cmd/estop 또는 cmd/estop_release만 허용됩니다");
        }
    }

    void forwardCommand(const Envelope &request, std::uint8_t operation)
    {
        if (!safetyCommandClient_->service_is_ready()) {
            respond(request, false, gateway_transport::err::kUnreachable,
                    "safety_manager가 준비되지 않았습니다");
            return;
        }

        auto command = std::make_shared<SafetyCommand::Request>();
        command->request_id = request.id.empty()
            ? "hmi-estop-" + std::to_string(++requestSequence_)
            : "hmi-estop-" + request.id;
        command->operator_id = "hmi";
        command->operation = operation;
        safetyCommandClient_->async_send_request(
            command, [this, request](rclcpp::Client<SafetyCommand>::SharedFuture future) {
                try {
                    const auto result = future.get();
                    if (result->accepted)
                        respond(request, true);
                    else
                        respond(request, false, result->reason_code, result->detail);
                } catch (const std::exception &error) {
                    respond(request, false, gateway_transport::err::kUnreachable,
                            std::string("safety_manager 응답 실패: ") + error.what());
                }
            });
    }

    void publishHeartbeat()
    {
        const bool alive = server_.isConnected() && receivedHeartbeat_
            && std::chrono::steady_clock::now() - lastHeartbeat_ <= heartbeatTimeout_;
        SafetyHeartbeat heartbeat;
        heartbeat.stamp = get_clock()->now();
        heartbeat.sequence = ++heartbeatSequence_;
        heartbeat.source = "estop_bridge";
        heartbeat.alive = alive;
        heartbeatPub_->publish(heartbeat);
    }

    void sendEnvelope(const Envelope &envelope)
    {
        Envelope stamped = envelope;
        stamped.robot = robotId_;
        server_.send(inspection::encodeFrame(stamped.toHeader()));
    }

    void respond(const Envelope &request, bool ok, const std::string &code = {},
                 const std::string &message = {})
    {
        sendEnvelope(gateway_transport::makeResponse(request, ok, code, message));
    }

    int port_ = 9091;
    std::string robotId_;
    std::chrono::milliseconds heartbeatTimeout_{1000};
    std::chrono::steady_clock::time_point lastHeartbeat_{};
    bool receivedHeartbeat_ = false;
    bool bindFailed_ = false;
    std::uint64_t heartbeatSequence_ = 0;
    std::uint64_t requestSequence_ = 0;

    TcpServer server_;
    rclcpp::Client<SafetyCommand>::SharedPtr safetyCommandClient_;
    rclcpp::Publisher<SafetyHeartbeat>::SharedPtr heartbeatPub_;
    rclcpp::TimerBase::SharedPtr bindRetryTimer_;
    rclcpp::TimerBase::SharedPtr linkTimer_;
    rclcpp::TimerBase::SharedPtr heartbeatTimer_;
};

}  // namespace

int main(int argc, char **argv)
{
    rclcpp::init(argc, argv);
    try {
        rclcpp::spin(std::make_shared<EstopBridgeNode>());
    } catch (const std::exception &error) {
        RCLCPP_FATAL(rclcpp::get_logger("estop_bridge"), "기동 실패: %s", error.what());
        rclcpp::shutdown();
        return 1;
    }
    rclcpp::shutdown();
    return 0;
}
