#include <chrono>
#include <functional>
#include <string>

#include "geometry_msgs/msg/twist.hpp"
#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/joint_state.hpp"
#include "std_msgs/msg/bool.hpp"
#include "std_msgs/msg/string.hpp"

namespace {

using namespace std::chrono_literals;
using SteadyTime = std::chrono::steady_clock::time_point;

class SafetyGateNode final : public rclcpp::Node {
public:
  SafetyGateNode() : Node("safety_gate") {
    const auto input_base_topic = declare_parameter<std::string>("input_base_topic", "/motion/base/cmd_vel");
    const auto output_base_topic = declare_parameter<std::string>("output_base_topic", "/motion/safe/cmd_vel");
    const auto input_arm_topic = declare_parameter<std::string>("input_arm_topic", "/motion/arm/joint_command");
    const auto output_arm_topic = declare_parameter<std::string>("output_arm_topic", "/motion/safe/arm/joint_command");
    const auto command_timeout_ms = declare_parameter<int>("command_timeout_ms", 300);
    const auto permit_timeout_ms = declare_parameter<int>("permit_timeout_ms", 250);
    const auto output_hz = declare_parameter<double>("output_hz", 20.0);
    arm_output_enabled_ = declare_parameter<bool>("arm_output_enabled", false);
    command_timeout_ = std::chrono::milliseconds(command_timeout_ms);
    permit_timeout_ = std::chrono::milliseconds(permit_timeout_ms);

    base_pub_ = create_publisher<geometry_msgs::msg::Twist>(output_base_topic, 20);
    arm_pub_ = create_publisher<sensor_msgs::msg::JointState>(output_arm_topic, 20);
    base_sub_ = create_subscription<geometry_msgs::msg::Twist>(
      input_base_topic, 20, std::bind(&SafetyGateNode::on_base_command, this, std::placeholders::_1));
    arm_sub_ = create_subscription<sensor_msgs::msg::JointState>(
      input_arm_topic, 20, std::bind(&SafetyGateNode::on_arm_command, this, std::placeholders::_1));
    permit_sub_ = create_subscription<std_msgs::msg::Bool>(
      "/safety/motion_permitted", 20, std::bind(&SafetyGateNode::on_permit, this, std::placeholders::_1));
    authority_sub_ = create_subscription<std_msgs::msg::String>(
      "/motion/authority", 20, std::bind(&SafetyGateNode::on_authority, this, std::placeholders::_1));

    const auto period = std::chrono::duration<double>(1.0 / output_hz);
    timer_ = create_wall_timer(std::chrono::duration_cast<std::chrono::milliseconds>(period),
      std::bind(&SafetyGateNode::tick, this));
    RCLCPP_WARN(get_logger(), "Safety gate output is %s; set output_base_topic:=/cmd_vel only after integration validation.",
      output_base_topic.c_str());
  }

private:
  void on_base_command(const geometry_msgs::msg::Twist::SharedPtr message) {
    base_command_ = *message;
    last_base_command_ = std::chrono::steady_clock::now();
  }

  void on_arm_command(const sensor_msgs::msg::JointState::SharedPtr message) {
    arm_command_ = *message;
    last_arm_command_ = std::chrono::steady_clock::now();
  }

  void on_permit(const std_msgs::msg::Bool::SharedPtr message) {
    motion_permitted_ = message->data;
    last_permit_ = std::chrono::steady_clock::now();
  }

  void on_authority(const std_msgs::msg::String::SharedPtr message) { authority_ = message->data; }

  bool permit_valid() const {
    return motion_permitted_ && std::chrono::steady_clock::now() - last_permit_ <= permit_timeout_;
  }

  void tick() {
    const auto now = std::chrono::steady_clock::now();
    geometry_msgs::msg::Twist output{};  // zero is the only fail-closed base command.
    if (permit_valid() && authority_ == "base_active" && now - last_base_command_ <= command_timeout_) {
      output = base_command_;
    }
    base_pub_->publish(output);

    // A zero JointState is not a safe stop for position-controlled arms.  The
    // production FR3 integration must use its own stop/mode interface.  Until
    // that is wired, arm output remains disabled by default.
    if (arm_output_enabled_ && permit_valid() && authority_ == "arm_active" &&
        now - last_arm_command_ <= command_timeout_) {
      arm_pub_->publish(arm_command_);
    }
  }

  bool motion_permitted_{false};
  bool arm_output_enabled_{false};
  std::string authority_{"none"};
  std::chrono::milliseconds command_timeout_{300};
  std::chrono::milliseconds permit_timeout_{250};
  SteadyTime last_permit_{};
  SteadyTime last_base_command_{};
  SteadyTime last_arm_command_{};
  geometry_msgs::msg::Twist base_command_{};
  sensor_msgs::msg::JointState arm_command_{};
  rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr base_pub_;
  rclcpp::Publisher<sensor_msgs::msg::JointState>::SharedPtr arm_pub_;
  rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr base_sub_;
  rclcpp::Subscription<sensor_msgs::msg::JointState>::SharedPtr arm_sub_;
  rclcpp::Subscription<std_msgs::msg::Bool>::SharedPtr permit_sub_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr authority_sub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace

int main(int argc, char ** argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<SafetyGateNode>());
  rclcpp::shutdown();
  return 0;
}
