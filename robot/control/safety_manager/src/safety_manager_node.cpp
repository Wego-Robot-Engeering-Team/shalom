// Copyright (c) 2026 WeGo Robotics. All rights reserved.

#include <chrono>
#include <functional>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/bool.hpp"
#include "std_msgs/msg/string.hpp"

#include "safety_manager/safety_fsm.hpp"

namespace {

using namespace std::chrono_literals;

class SafetyManagerNode final : public rclcpp::Node {
public:
  SafetyManagerNode() : Node("safety_manager") {
    require_heartbeat_ = declare_parameter<bool>("require_external_heartbeat", false);
    const auto heartbeat_timeout_ms = declare_parameter<int>("heartbeat_timeout_ms", 500);
    heartbeat_timeout_ = std::chrono::milliseconds(heartbeat_timeout_ms);

    state_pub_ = create_publisher<std_msgs::msg::String>("/safety/state", rclcpp::QoS(1).transient_local());
    permit_pub_ = create_publisher<std_msgs::msg::Bool>("/safety/motion_permitted", rclcpp::QoS(1).transient_local());
    event_sub_ = create_subscription<std_msgs::msg::String>(
      "/safety/event", 20, std::bind(&SafetyManagerNode::on_event, this, std::placeholders::_1));
    estop_sub_ = create_subscription<std_msgs::msg::Bool>(
      "/safety/physical_estop_active", 20,
      std::bind(&SafetyManagerNode::on_estop, this, std::placeholders::_1));
    heartbeat_sub_ = create_subscription<std_msgs::msg::Bool>(
      "/safety/heartbeat", 20, std::bind(&SafetyManagerNode::on_heartbeat, this, std::placeholders::_1));

    last_heartbeat_ = std::chrono::steady_clock::now();
    timer_ = create_wall_timer(50ms, std::bind(&SafetyManagerNode::tick, this));
    publish();
  }

private:
  void on_event(const std_msgs::msg::String::SharedPtr message) {
    using safety_manager::SafetyEvent;
    const auto & value = message->data;
    if (value == "request_stop") dispatch(SafetyEvent::kRequestStop);
    else if (value == "health_fault") dispatch(SafetyEvent::kHealthFault);
    else if (value == "clear_fault") dispatch(SafetyEvent::kClearFault);
    else if (value == "resume") dispatch(SafetyEvent::kResume);
    else RCLCPP_WARN(get_logger(), "Ignored unknown safety event: %s", value.c_str());
  }

  void on_estop(const std_msgs::msg::Bool::SharedPtr message) {
    using safety_manager::SafetyEvent;
    dispatch(message->data ? SafetyEvent::kEmergencyStopPressed : SafetyEvent::kEmergencyStopReleased);
  }

  void on_heartbeat(const std_msgs::msg::Bool::SharedPtr) { last_heartbeat_ = std::chrono::steady_clock::now(); }

  void tick() {
    if (require_heartbeat_ && std::chrono::steady_clock::now() - last_heartbeat_ > heartbeat_timeout_) {
      dispatch(safety_manager::SafetyEvent::kWatchdogTimeout);
    }
    publish();
  }

  void dispatch(safety_manager::SafetyEvent event) {
    const auto transition = fsm_.dispatch(event);
    if (transition.accepted) {
      RCLCPP_WARN(get_logger(), "Safety %s -> %s: %s",
        safety_manager::to_string(transition.from), safety_manager::to_string(transition.to), transition.reason);
      publish();
    }
  }

  void publish() {
    std_msgs::msg::String state;
    state.data = safety_manager::to_string(fsm_.state());
    state_pub_->publish(state);
    std_msgs::msg::Bool permitted;
    permitted.data = fsm_.motion_permitted();
    permit_pub_->publish(permitted);
  }

  bool require_heartbeat_{};
  std::chrono::milliseconds heartbeat_timeout_{500};
  std::chrono::steady_clock::time_point last_heartbeat_{};
  safety_manager::SafetyFsm fsm_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr state_pub_;
  rclcpp::Publisher<std_msgs::msg::Bool>::SharedPtr permit_pub_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr event_sub_;
  rclcpp::Subscription<std_msgs::msg::Bool>::SharedPtr estop_sub_;
  rclcpp::Subscription<std_msgs::msg::Bool>::SharedPtr heartbeat_sub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace

int main(int argc, char ** argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<SafetyManagerNode>());
  rclcpp::shutdown();
  return 0;
}
