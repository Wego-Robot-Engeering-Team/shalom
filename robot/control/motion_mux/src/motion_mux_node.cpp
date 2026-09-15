#include <array>
#include <chrono>
#include <functional>
#include <string>

#include "geometry_msgs/msg/twist.hpp"
#include "rclcpp/rclcpp.hpp"

namespace {

using namespace std::chrono_literals;

struct Source {
  const char * name;
  geometry_msgs::msg::Twist command{};
  std::chrono::steady_clock::time_point received{};
};

class MotionMuxNode final : public rclcpp::Node {
public:
  MotionMuxNode() : Node("motion_mux") {
    const auto output_topic = declare_parameter<std::string>("output_topic", "/motion/base/cmd_vel");
    const auto source_timeout_ms = declare_parameter<int>("source_timeout_ms", 300);
    const auto output_hz = declare_parameter<double>("output_hz", 20.0);
    source_timeout_ = std::chrono::milliseconds(source_timeout_ms);

    // Order is policy: first fresh source wins. Changing priorities is a launch
    // configuration decision, not an incidental ROS publisher race.
    constexpr std::array<const char *, 5> names{
      "teleop", "mission", "stair", "dock", "nav"};
    constexpr std::array<const char *, 5> defaults{
      "/motion/teleop/cmd_vel", "/motion/mission/cmd_vel", "/motion/stair/cmd_vel",
      "/motion/dock/cmd_vel", "/motion/nav/cmd_vel"};
    for (std::size_t i = 0; i < sources_.size(); ++i) {
      sources_[i].name = names[i];
      const auto topic = declare_parameter<std::string>(std::string(names[i]) + "_topic", defaults[i]);
      subscriptions_[i] = create_subscription<geometry_msgs::msg::Twist>(
        topic, 20, [this, i](const geometry_msgs::msg::Twist::SharedPtr message) {
          sources_[i].command = *message;
          sources_[i].received = std::chrono::steady_clock::now();
        });
    }
    output_pub_ = create_publisher<geometry_msgs::msg::Twist>(output_topic, 20);
    const auto period = std::chrono::duration<double>(1.0 / output_hz);
    timer_ = create_wall_timer(std::chrono::duration_cast<std::chrono::milliseconds>(period),
      std::bind(&MotionMuxNode::tick, this));
  }

private:
  void tick() {
    const auto now = std::chrono::steady_clock::now();
    geometry_msgs::msg::Twist output{};
    for (const auto & source : sources_) {
      if (now - source.received <= source_timeout_) {
        output = source.command;
        break;
      }
    }
    output_pub_->publish(output);
  }

  std::array<Source, 5> sources_{};
  std::array<rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr, 5> subscriptions_{};
  std::chrono::milliseconds source_timeout_{300};
  rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr output_pub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace

int main(int argc, char ** argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<MotionMuxNode>());
  rclcpp::shutdown();
  return 0;
}
