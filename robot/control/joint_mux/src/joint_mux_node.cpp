// Copyright (c) 2026 WeGo Robotics. All rights reserved.

#include <array>
#include <chrono>
#include <functional>
#include <string>

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/joint_state.hpp>

namespace {

using namespace std::chrono_literals;

struct Source {
  const char * name = "";
  sensor_msgs::msg::JointState command{};
  std::chrono::steady_clock::time_point received{};
};

class JointMuxNode final : public rclcpp::Node {
public:
  JointMuxNode() : Node("joint_mux")
  {
    const auto output_topic = declare_parameter<std::string>(
      "output_topic", "/motion/arm/joint_command");
    const auto source_timeout_ms = declare_parameter<int>("source_timeout_ms", 300);
    const auto output_hz = declare_parameter<double>("output_hz", 20.0);
    source_timeout_ = std::chrono::milliseconds(source_timeout_ms);

    // Priority is the array order. No two publishers ever race on the final
    // arm topic: a fresh teleop command wins, then an operator hold, then the
    // FR3 behaviour tree. Each source must renew its own lease.
    constexpr std::array<const char *, 3> names{"teleop", "manual_hold", "fr3_bt"};
    constexpr std::array<const char *, 3> defaults{
      "/motion/arm/joint_command/teleop",
      "/motion/arm/joint_command/manual_hold",
      "/motion/arm/joint_command/fr3"};
    for (std::size_t i = 0; i < sources_.size(); ++i) {
      sources_[i].name = names[i];
      const auto topic = declare_parameter<std::string>(std::string(names[i]) + "_topic", defaults[i]);
      subscriptions_[i] = create_subscription<sensor_msgs::msg::JointState>(topic, 20,
        [this, i](const sensor_msgs::msg::JointState::SharedPtr message) {
          sources_[i].command = *message;
          sources_[i].received = std::chrono::steady_clock::now();
        });
    }
    output_pub_ = create_publisher<sensor_msgs::msg::JointState>(output_topic, 20);
    const auto period = std::chrono::duration<double>(1.0 / output_hz);
    timer_ = create_wall_timer(std::chrono::duration_cast<std::chrono::milliseconds>(period),
      std::bind(&JointMuxNode::tick, this));
  }

private:
  void tick()
  {
    const auto now = std::chrono::steady_clock::now();
    for (const auto &source : sources_) {
      if (now - source.received <= source_timeout_) {
        output_pub_->publish(source.command);
        return;
      }
    }
    // No synthetic zero JointState: position-controlled arms must be stopped
    // by their vendor stop/mode interface, never by inventing a joint target.
  }

  std::array<Source, 3> sources_{};
  std::array<rclcpp::Subscription<sensor_msgs::msg::JointState>::SharedPtr, 3> subscriptions_{};
  std::chrono::milliseconds source_timeout_{300};
  rclcpp::Publisher<sensor_msgs::msg::JointState>::SharedPtr output_pub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<JointMuxNode>());
  rclcpp::shutdown();
  return 0;
}
