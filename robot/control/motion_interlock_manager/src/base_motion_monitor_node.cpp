// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include <chrono>
#include <cmath>
#include <functional>
#include <memory>
#include <string>

#include "geometry_msgs/msg/twist.hpp"
#include "nav_msgs/msg/odometry.hpp"
#include "rclcpp/rclcpp.hpp"
#include "shalom_interfaces/msg/motion_stopped.hpp"

#include "motion_interlock_manager/base_stop_detector.hpp"

namespace {

using namespace std::chrono_literals;
using MotionStopped = shalom_interfaces::msg::MotionStopped;
using BaseStopDetector = motion_interlock_manager::BaseStopDetector;

class BaseMotionMonitorNode final : public rclcpp::Node {
public:
  BaseMotionMonitorNode()
  : Node("base_motion_monitor"), detector_(make_config()) {
    const auto command_topic = declare_parameter<std::string>("command_topic", "/cmd_vel");
    const auto odometry_topic = declare_parameter<std::string>("odometry_topic", "/b2/odom");
    const auto output_hz = declare_parameter<double>("output_hz", 20.0);

    stopped_pub_ = create_publisher<MotionStopped>("/motion/stopped", 20);
    command_sub_ = create_subscription<geometry_msgs::msg::Twist>(
      command_topic, 20,
      std::bind(&BaseMotionMonitorNode::on_command, this, std::placeholders::_1));
    odometry_sub_ = create_subscription<nav_msgs::msg::Odometry>(
      odometry_topic, rclcpp::SensorDataQoS(),
      std::bind(&BaseMotionMonitorNode::on_odometry, this, std::placeholders::_1));

    const auto period = std::chrono::duration<double>(1.0 / output_hz);
    timer_ = create_wall_timer(
      std::chrono::duration_cast<std::chrono::milliseconds>(period),
      std::bind(&BaseMotionMonitorNode::tick, this));

    RCLCPP_INFO(
      get_logger(), "Base stop monitor: command=%s odometry=%s",
      command_topic.c_str(), odometry_topic.c_str());
  }

private:
  BaseStopDetector::Config make_config() {
    BaseStopDetector::Config config;
    config.linear_threshold = declare_parameter<double>("linear_threshold", 0.03);
    config.angular_threshold = declare_parameter<double>("angular_threshold", 0.05);
    config.command_timeout = std::chrono::milliseconds(
      declare_parameter<int>("command_timeout_ms", 250));
    config.odometry_timeout = std::chrono::milliseconds(
      declare_parameter<int>("odometry_timeout_ms", 500));
    config.settle_time = std::chrono::milliseconds(
      declare_parameter<int>("settle_time_ms", 200));
    return config;
  }

  void on_command(const geometry_msgs::msg::Twist::SharedPtr message) {
    constexpr double epsilon = 1e-6;
    const bool zero = std::abs(message->linear.x) <= epsilon &&
      std::abs(message->linear.y) <= epsilon &&
      std::abs(message->linear.z) <= epsilon &&
      std::abs(message->angular.x) <= epsilon &&
      std::abs(message->angular.y) <= epsilon &&
      std::abs(message->angular.z) <= epsilon;
    detector_.observe_command(zero, BaseStopDetector::Clock::now());
  }

  void on_odometry(const nav_msgs::msg::Odometry::SharedPtr message) {
    const auto & twist = message->twist.twist;
    detector_.observe_odometry(
      std::hypot(twist.linear.x, twist.linear.y), std::abs(twist.angular.z),
      BaseStopDetector::Clock::now());
  }

  void tick() {
    MotionStopped message;
    message.stamp = now();
    message.sequence = ++sequence_;
    message.resource = MotionStopped::BASE;
    message.stopped = detector_.stopped(BaseStopDetector::Clock::now());
    message.source = "base_motion_monitor";
    message.detail = message.stopped
      ? "fresh final command and measured base velocity are stably zero"
      : "base is moving or command/odometry feedback is stale";
    stopped_pub_->publish(message);
  }

  BaseStopDetector detector_;
  uint64_t sequence_{0};
  rclcpp::Publisher<MotionStopped>::SharedPtr stopped_pub_;
  rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr command_sub_;
  rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr odometry_sub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace

int main(int argc, char ** argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<BaseMotionMonitorNode>());
  rclcpp::shutdown();
  return 0;
}
