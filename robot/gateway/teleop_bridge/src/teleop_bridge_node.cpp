// Copyright (c) 2026 WeGo Robotics. All rights reserved.

#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>

#include <geometry_msgs/msg/twist.hpp>
#include <nlohmann/json.hpp>
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/bool.hpp>

namespace {

using namespace std::chrono_literals;
using json = nlohmann::json;

constexpr std::size_t kMaxDatagramBytes = 1024;

bool setNonBlocking(const int fd)
{
  const int flags = ::fcntl(fd, F_GETFL, 0);
  return flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

class TeleopBridgeNode final : public rclcpp::Node {
public:
  TeleopBridgeNode() : Node("teleop_bridge")
  {
    const auto output_topic = declare_parameter<std::string>(
      "output_topic", "/motion/teleop/cmd_vel");
    const auto command_timeout_ms = declare_parameter<int>("command_timeout_ms", 300);
    const auto output_hz = declare_parameter<double>("output_hz", 20.0);
    udp_port_ = declare_parameter<int>("udp_port", 9090);
    robot_id_ = declare_parameter<std::string>("robot_id", "R1");
    allowed_peer_ = declare_parameter<std::string>("allowed_peer", "");
    max_linear_x_ = declare_parameter<double>("max_linear_x", 0.60);
    max_linear_y_ = declare_parameter<double>("max_linear_y", 0.40);
    max_angular_z_ = declare_parameter<double>("max_angular_z", 0.80);
    command_timeout_ = std::chrono::milliseconds(command_timeout_ms);

    output_pub_ = create_publisher<geometry_msgs::msg::Twist>(output_topic, 20);
    // ROS inputs remain useful for replay tests and an on-robot joystick. They
    // cannot bypass the same deadman lease used by the UDP HMI path.
    command_sub_ = create_subscription<geometry_msgs::msg::Twist>(
      "/teleop/input/cmd_vel", 20,
      std::bind(&TeleopBridgeNode::onRosCommand, this, std::placeholders::_1));
    deadman_sub_ = create_subscription<std_msgs::msg::Bool>(
      "/teleop/input/deadman", 20,
      std::bind(&TeleopBridgeNode::onRosDeadman, this, std::placeholders::_1));

    openUdpSocket();
    const auto period = std::chrono::duration<double>(1.0 / output_hz);
    timer_ = create_wall_timer(std::chrono::duration_cast<std::chrono::milliseconds>(period),
      std::bind(&TeleopBridgeNode::tick, this));
  }

  ~TeleopBridgeNode() override
  {
    if (udp_fd_ >= 0)
      ::close(udp_fd_);
  }

private:
  void openUdpSocket()
  {
    if (udp_port_ <= 0 || udp_port_ > 65535) {
      RCLCPP_ERROR(get_logger(), "UDP teleop 포트가 올바르지 않습니다: %d", udp_port_);
      return;
    }
    udp_fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd_ < 0) {
      RCLCPP_ERROR(get_logger(), "UDP teleop socket 생성 실패: %s", std::strerror(errno));
      return;
    }
    int one = 1;
    ::setsockopt(udp_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<std::uint16_t>(udp_port_));
    if (::bind(udp_fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0
        || !setNonBlocking(udp_fd_)) {
      RCLCPP_ERROR(get_logger(), "UDP teleop 포트 %d를 열지 못했습니다: %s", udp_port_,
        std::strerror(errno));
      ::close(udp_fd_);
      udp_fd_ = -1;
      return;
    }
    if (allowed_peer_.empty()) {
      RCLCPP_WARN(get_logger(),
        "UDP teleop은 allowed_peer가 비어 있어 차단됩니다. 현장 HMI IP를 설정해야 합니다.");
    }
    RCLCPP_INFO(get_logger(), "UDP teleop 대기 중 — UDP %d, robot_id=%s", udp_port_,
      robot_id_.c_str());
  }

  static double clamp(const double value, const double limit)
  {
    return std::max(-limit, std::min(limit, value));
  }

  void onRosCommand(const geometry_msgs::msg::Twist::SharedPtr message)
  {
    command_ = *message;
    last_command_ = std::chrono::steady_clock::now();
  }

  void onRosDeadman(const std_msgs::msg::Bool::SharedPtr message)
  {
    ros_deadman_ = message->data;
  }

  void drainUdp()
  {
    if (udp_fd_ < 0)
      return;
    char bytes[kMaxDatagramBytes];
    for (;;) {
      sockaddr_in peer{};
      socklen_t peer_len = sizeof(peer);
      const auto received = ::recvfrom(udp_fd_, bytes, sizeof(bytes), 0,
        reinterpret_cast<sockaddr *>(&peer), &peer_len);
      if (received < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
          return;
        if (errno == EINTR)
          continue;
        RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000,
          "UDP teleop 수신 실패: %s", std::strerror(errno));
        return;
      }
      if (received == 0 || received == static_cast<ssize_t>(sizeof(bytes)))
        continue;  // 빈 패킷과 잘린 패킷은 명령이 아니다.

      char peer_ip[INET_ADDRSTRLEN]{};
      ::inet_ntop(AF_INET, &peer.sin_addr, peer_ip, sizeof(peer_ip));
      if (allowed_peer_.empty() || allowed_peer_ != peer_ip)
        continue;

      try {
        const auto packet = json::parse(bytes, bytes + received);
        if (packet.value("v", 0) != 1 || packet.value("t", std::string{}) != "teleop"
            || packet.value("robot", std::string{}) != robot_id_)
          continue;
        const auto seq = packet.value("seq", std::int64_t{-1});
        if (seq <= last_udp_sequence_)
          continue;  // 지연·재전송된 UDP 패킷은 과거 명령을 되살리지 못한다.
        last_udp_sequence_ = seq;

        udp_deadman_ = packet.value("deadman", false);
        command_.linear.x = clamp(packet.value("vx", 0.0), max_linear_x_);
        command_.linear.y = clamp(packet.value("vy", 0.0), max_linear_y_);
        command_.angular.z = clamp(packet.value("wz", 0.0), max_angular_z_);
        last_command_ = std::chrono::steady_clock::now();
      } catch (const json::exception &) {
        // UDP는 신뢰하지 않는다. 형식 오류는 조용히 버리고 마지막 유효
        // 명령의 lease가 만료되게 둔다.
      }
    }
  }

  void tick()
  {
    drainUdp();
    const auto now = std::chrono::steady_clock::now();
    if ((udp_deadman_ || ros_deadman_) && now - last_command_ <= command_timeout_) {
      output_pub_->publish(command_);
      return;
    }
    // mux는 fresh source만 선택한다. zero를 계속 발행해 teleop 우선권을
    // 붙잡지 않고 lease가 끝나면 자동/미션 입력이 다시 선택되게 한다.
    udp_deadman_ = false;
    if (now - last_command_ > command_timeout_)
      last_udp_sequence_ = -1;  // HMI 재시작 후 sequence는 다시 0부터여도 된다.
  }

  int udp_fd_ = -1;
  int udp_port_ = 9090;
  std::string robot_id_{"R1"};
  std::string allowed_peer_;
  double max_linear_x_ = 0.60;
  double max_linear_y_ = 0.40;
  double max_angular_z_ = 0.80;
  bool udp_deadman_ = false;
  bool ros_deadman_ = false;
  std::int64_t last_udp_sequence_ = -1;
  std::chrono::milliseconds command_timeout_{300};
  std::chrono::steady_clock::time_point last_command_{};
  geometry_msgs::msg::Twist command_{};
  rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr output_pub_;
  rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr command_sub_;
  rclcpp::Subscription<std_msgs::msg::Bool>::SharedPtr deadman_sub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<TeleopBridgeNode>());
  rclcpp::shutdown();
  return 0;
}
