// Copyright (c) 2026 WeGo Robotics. All rights reserved.

#include <functional>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/bool.hpp"
#include "std_msgs/msg/string.hpp"

#include "motion_interlock_manager/motion_interlock.hpp"

namespace {

class MotionInterlockManagerNode final : public rclcpp::Node {
public:
  MotionInterlockManagerNode() : Node("motion_interlock_manager") {
    authority_pub_ = create_publisher<std_msgs::msg::String>(
      "/motion/authority", rclcpp::QoS(1).transient_local());
    request_sub_ = create_subscription<std_msgs::msg::String>(
      "/motion/request_authority", 20,
      std::bind(&MotionInterlockManagerNode::on_request, this, std::placeholders::_1));
    base_stopped_sub_ = create_subscription<std_msgs::msg::Bool>(
      "/motion/base/stopped", 20,
      std::bind(&MotionInterlockManagerNode::on_base_stopped, this, std::placeholders::_1));
    arm_stopped_sub_ = create_subscription<std_msgs::msg::Bool>(
      "/motion/arm/stopped", 20,
      std::bind(&MotionInterlockManagerNode::on_arm_stopped, this, std::placeholders::_1));
    publish();
  }

private:
  void on_request(const std_msgs::msg::String::SharedPtr message) {
    using motion_interlock_manager::Request;
    if (message->data == "base") dispatch(interlock_.request(Request::kBase));
    else if (message->data == "arm") dispatch(interlock_.request(Request::kArm));
    else if (message->data == "release") dispatch(interlock_.request(Request::kRelease));
    else RCLCPP_WARN(get_logger(), "Ignored unknown motion authority request: %s", message->data.c_str());
  }

  void on_base_stopped(const std_msgs::msg::Bool::SharedPtr message) {
    if (message->data) dispatch(interlock_.base_stopped());
  }

  void on_arm_stopped(const std_msgs::msg::Bool::SharedPtr message) {
    if (message->data) dispatch(interlock_.arm_stopped());
  }

  void dispatch(const motion_interlock_manager::Transition & transition) {
    if (!transition.accepted) return;
    RCLCPP_INFO(get_logger(), "Motion authority %s -> %s: %s",
      motion_interlock_manager::to_string(transition.from),
      motion_interlock_manager::to_string(transition.to), transition.reason);
    publish();
  }

  void publish() {
    std_msgs::msg::String state;
    state.data = motion_interlock_manager::to_string(interlock_.state());
    authority_pub_->publish(state);
  }

  motion_interlock_manager::MotionInterlock interlock_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr authority_pub_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr request_sub_;
  rclcpp::Subscription<std_msgs::msg::Bool>::SharedPtr base_stopped_sub_;
  rclcpp::Subscription<std_msgs::msg::Bool>::SharedPtr arm_stopped_sub_;
};

}  // namespace

int main(int argc, char ** argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<MotionInterlockManagerNode>());
  rclcpp::shutdown();
  return 0;
}
