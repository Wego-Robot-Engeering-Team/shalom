// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include <chrono>
#include <functional>
#include <stdexcept>
#include <string>

#include "geometry_msgs/msg/twist.hpp"
#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/joint_state.hpp"
#include "shalom_interfaces/msg/motion_authority.hpp"
#include "shalom_interfaces/msg/safety_state.hpp"
#include "std_msgs/msg/bool.hpp"

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
    const auto state_timeout_ms = declare_parameter<int>("safety_state_timeout_ms", 250);
    const auto authority_timeout_ms = declare_parameter<int>("authority_timeout_ms", 500);
    const auto inhibit_timeout_ms = declare_parameter<int>("base_inhibit_timeout_ms", 250);
    const auto output_hz = declare_parameter<double>("output_hz", 20.0);
    if (authority_timeout_ms <= 0 || inhibit_timeout_ms <= 0) {
      throw std::invalid_argument("authority and base inhibit timeouts must be positive");
    }
    arm_output_enabled_ = declare_parameter<bool>("arm_output_enabled", false);
    command_timeout_ = std::chrono::milliseconds(command_timeout_ms);
    state_timeout_ = std::chrono::milliseconds(state_timeout_ms);
    authority_timeout_ = std::chrono::milliseconds(authority_timeout_ms);
    inhibit_timeout_ = std::chrono::milliseconds(inhibit_timeout_ms);

    base_pub_ = create_publisher<geometry_msgs::msg::Twist>(output_base_topic, 20);
    arm_pub_ = create_publisher<sensor_msgs::msg::JointState>(output_arm_topic, 20);
    base_sub_ = create_subscription<geometry_msgs::msg::Twist>(
      input_base_topic, 20, std::bind(&SafetyGateNode::on_base_command, this, std::placeholders::_1));
    arm_sub_ = create_subscription<sensor_msgs::msg::JointState>(
      input_arm_topic, 20, std::bind(&SafetyGateNode::on_arm_command, this, std::placeholders::_1));
    safety_sub_ = create_subscription<shalom_interfaces::msg::SafetyState>(
      "/safety/state", rclcpp::QoS(1).reliable().transient_local(),
      std::bind(&SafetyGateNode::on_safety, this, std::placeholders::_1));
    authority_sub_ = create_subscription<shalom_interfaces::msg::MotionAuthority>(
      "/motion/authority", rclcpp::QoS(1).reliable().transient_local(),
      std::bind(&SafetyGateNode::on_authority, this, std::placeholders::_1));
    inhibit_sub_ = create_subscription<std_msgs::msg::Bool>(
      "/motion/base/inhibit", 10,
      std::bind(&SafetyGateNode::on_base_inhibit, this, std::placeholders::_1));
    source_inhibit_sub_ = create_subscription<std_msgs::msg::Bool>(
      "/motion/base_source/inhibit", 10,
      std::bind(&SafetyGateNode::on_source_inhibit, this, std::placeholders::_1));

    const auto period = std::chrono::duration<double>(1.0 / output_hz);
    timer_ = create_wall_timer(std::chrono::duration_cast<std::chrono::milliseconds>(period),
      std::bind(&SafetyGateNode::tick, this));
    RCLCPP_INFO(get_logger(), "Safety gate is the sole base-command publisher: %s",
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

  void on_safety(const shalom_interfaces::msg::SafetyState::SharedPtr message) {
    safety_state_ = message->state;
    safety_motion_permitted_ = message->motion_permitted;
    last_state_ = std::chrono::steady_clock::now();
  }

  void on_authority(const shalom_interfaces::msg::MotionAuthority::SharedPtr message) {
    authority_ = message->state;
    last_authority_ = std::chrono::steady_clock::now();
  }

  void on_base_inhibit(const std_msgs::msg::Bool::SharedPtr message) {
    const auto now = std::chrono::steady_clock::now();
    reset_base_command_on_inhibit_transition(base_inhibit_, last_inhibit_, message->data, now);
    base_inhibit_ = message->data;
    last_inhibit_ = now;
  }

  void on_source_inhibit(const std_msgs::msg::Bool::SharedPtr message) {
    const auto now = std::chrono::steady_clock::now();
    reset_base_command_on_inhibit_transition(
      source_inhibit_, last_source_inhibit_, message->data, now);
    source_inhibit_ = message->data;
    last_source_inhibit_ = now;
  }

  void reset_base_command_on_inhibit_transition(
      bool previous, SteadyTime last_update, bool incoming, SteadyTime now) {
    // A transition or heartbeat recovery on either independent inhibit must
    // not replay a Twist cached before it. Require a new mux output.
    if (previous != incoming || now - last_update > inhibit_timeout_) {
      base_command_ = geometry_msgs::msg::Twist{};
      last_base_command_ = SteadyTime{};
    }
  }

  /// What the safety state means for the output, per the control-plane design:
  ///
  ///   normal           pass only when motion_permitted is true; otherwise zero
  ///   controlled_stop  publish zero -- a normal pause, the robot stays up
  ///   fault            publish zero -- same, it is not an emergency stop
  ///   e_stop_latched   publish nothing at all
  ///
  /// The last one is the important distinction. Publishing a zero is still
  /// commanding the robot; an emergency stop must not command anything, and
  /// the driver's own command timeout is what brings the robot down. If the
  /// safety manager goes silent we treat it as a latched stop, because a gate
  /// that keeps passing commands on a dead supervisor is not a gate.
  enum class Output { kPass, kZero, kBlock };

  Output decide() const {
    if (std::chrono::steady_clock::now() - last_state_ > state_timeout_)
      return Output::kBlock;
    using SafetyState = shalom_interfaces::msg::SafetyState;
    if (safety_state_ == SafetyState::NORMAL)
      return safety_motion_permitted_ ? Output::kPass : Output::kZero;
    if (safety_state_ == SafetyState::E_STOP_LATCHED) return Output::kBlock;
    if (safety_state_ == SafetyState::INITIALIZING ||
        safety_state_ == SafetyState::CONTROLLED_STOP ||
        safety_state_ == SafetyState::FAULT) {
      return Output::kZero;
    }
    return Output::kBlock;
  }

  void tick() {
    const auto now = std::chrono::steady_clock::now();
    const Output decision = decide();
    if (decision == Output::kBlock)
      return;

    const bool authority_fresh = now - last_authority_ <= authority_timeout_;
    geometry_msgs::msg::Twist output{};  // zero is the only fail-closed base command.
    if (decision == Output::kPass &&
        !base_inhibit_ && now - last_inhibit_ <= inhibit_timeout_ &&
        !source_inhibit_ && now - last_source_inhibit_ <= inhibit_timeout_ &&
        authority_fresh &&
        authority_ == shalom_interfaces::msg::MotionAuthority::BASE_ACTIVE &&
        now - last_base_command_ <= command_timeout_) {
      output = base_command_;
    }
    base_pub_->publish(output);

    // A zero JointState is not a safe stop for position-controlled arms.  The
    // production FR3 integration must use its own stop/mode interface.  Until
    // that is wired, arm output remains disabled by default.
    if (arm_output_enabled_ && decision == Output::kPass && authority_fresh &&
        authority_ == shalom_interfaces::msg::MotionAuthority::ARM_ACTIVE &&
        now - last_arm_command_ <= command_timeout_) {
      arm_pub_->publish(arm_command_);
    }
  }

  uint8_t safety_state_{shalom_interfaces::msg::SafetyState::INITIALIZING};
  bool safety_motion_permitted_{false};
  bool arm_output_enabled_{false};
  bool base_inhibit_{true};
  bool source_inhibit_{true};
  uint8_t authority_{shalom_interfaces::msg::MotionAuthority::NONE};
  std::chrono::milliseconds command_timeout_{300};
  std::chrono::milliseconds state_timeout_{250};
  std::chrono::milliseconds authority_timeout_{500};
  std::chrono::milliseconds inhibit_timeout_{250};
  SteadyTime last_state_{};
  SteadyTime last_authority_{};
  SteadyTime last_inhibit_{};
  SteadyTime last_source_inhibit_{};
  SteadyTime last_base_command_{};
  SteadyTime last_arm_command_{};
  geometry_msgs::msg::Twist base_command_{};
  sensor_msgs::msg::JointState arm_command_{};
  rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr base_pub_;
  rclcpp::Publisher<sensor_msgs::msg::JointState>::SharedPtr arm_pub_;
  rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr base_sub_;
  rclcpp::Subscription<sensor_msgs::msg::JointState>::SharedPtr arm_sub_;
  rclcpp::Subscription<shalom_interfaces::msg::SafetyState>::SharedPtr safety_sub_;
  rclcpp::Subscription<shalom_interfaces::msg::MotionAuthority>::SharedPtr authority_sub_;
  rclcpp::Subscription<std_msgs::msg::Bool>::SharedPtr inhibit_sub_;
  rclcpp::Subscription<std_msgs::msg::Bool>::SharedPtr source_inhibit_sub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace

int main(int argc, char ** argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<SafetyGateNode>());
  rclcpp::shutdown();
  return 0;
}
