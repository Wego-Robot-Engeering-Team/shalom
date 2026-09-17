// Copyright (c) 2026 WeGo Robotics. All rights reserved.

#include <chrono>
#include <functional>
#include <string>

#include "geometry_msgs/msg/twist.hpp"
#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/bool.hpp"

namespace {

using namespace std::chrono_literals;

class TeleopBridgeNode final : public rclcpp::Node {
public:
  TeleopBridgeNode() : Node("teleop_bridge") {
    const auto input_topic = declare_parameter<std::string>("input_topic", "/teleop/input/cmd_vel");
    const auto output_topic = declare_parameter<std::string>("output_topic", "/motion/teleop/cmd_vel");
    const auto command_timeout_ms = declare_parameter<int>("command_timeout_ms", 300);
    const auto output_hz = declare_parameter<double>("output_hz", 20.0);
    command_timeout_ = std::chrono::milliseconds(command_timeout_ms);

    output_pub_ = create_publisher<geometry_msgs::msg::Twist>(output_topic, 20);
    command_sub_ = create_subscription<geometry_msgs::msg::Twist>(
      input_topic, 20, std::bind(&TeleopBridgeNode::on_command, this, std::placeholders::_1));
    deadman_sub_ = create_subscription<std_msgs::msg::Bool>(
      "/teleop/input/deadman", 20, std::bind(&TeleopBridgeNode::on_deadman, this, std::placeholders::_1));
    const auto period = std::chrono::duration<double>(1.0 / output_hz);
    timer_ = create_wall_timer(std::chrono::duration_cast<std::chrono::milliseconds>(period),
      std::bind(&TeleopBridgeNode::tick, this));
  }

private:
  void on_command(const geometry_msgs::msg::Twist::SharedPtr message) {
    command_ = *message;
    last_command_ = std::chrono::steady_clock::now();
  }

  void on_deadman(const std_msgs::msg::Bool::SharedPtr message) { deadman_ = message->data; }

  void tick() {
    if (deadman_ && std::chrono::steady_clock::now() - last_command_ <= command_timeout_) {
      output_pub_->publish(command_);
    }
  }

  bool deadman_{false};
  std::chrono::milliseconds command_timeout_{300};
  std::chrono::steady_clock::time_point last_command_{};
  geometry_msgs::msg::Twist command_{};
  rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr output_pub_;
  rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr command_sub_;
  rclcpp::Subscription<std_msgs::msg::Bool>::SharedPtr deadman_sub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace

int main(int argc, char ** argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<TeleopBridgeNode>());
  rclcpp::shutdown();
  return 0;
}
