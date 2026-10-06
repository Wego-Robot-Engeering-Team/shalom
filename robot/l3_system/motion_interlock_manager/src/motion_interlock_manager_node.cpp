// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include <chrono>
#include <deque>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "rclcpp/rclcpp.hpp"
#include "shalom_interfaces/msg/motion_authority.hpp"
#include "shalom_interfaces/msg/motion_stopped.hpp"
#include "shalom_interfaces/msg/safety_event.hpp"
#include "shalom_interfaces/srv/authority_request.hpp"

#include "motion_interlock_manager/motion_interlock.hpp"

namespace {

using namespace std::chrono_literals;
using AuthorityRequest = shalom_interfaces::srv::AuthorityRequest;
using MotionAuthorityMsg = shalom_interfaces::msg::MotionAuthority;
using MotionStopped = shalom_interfaces::msg::MotionStopped;
using SafetyEvent = shalom_interfaces::msg::SafetyEvent;
using CoreAuthority = motion_interlock_manager::MotionAuthority;

uint8_t wire_authority(CoreAuthority state) {
  switch (state) {
    case CoreAuthority::None: return MotionAuthorityMsg::NONE;
    case CoreAuthority::BaseActive: return MotionAuthorityMsg::BASE_ACTIVE;
    case CoreAuthority::BaseStopping: return MotionAuthorityMsg::BASE_STOPPING;
    case CoreAuthority::ArmActive: return MotionAuthorityMsg::ARM_ACTIVE;
    case CoreAuthority::ArmStopping: return MotionAuthorityMsg::ARM_STOPPING;
  }
  return MotionAuthorityMsg::NONE;
}

class MotionInterlockManagerNode final : public rclcpp::Node {
public:
  MotionInterlockManagerNode() : Node("motion_interlock_manager") {
    transition_timeout_ = std::chrono::milliseconds(
      declare_parameter<int>("transition_timeout_ms", 1000));
    stopped_feedback_timeout_ = std::chrono::milliseconds(
      declare_parameter<int>("stopped_feedback_timeout_ms", 500));
    const auto state_publish_period_ms = declare_parameter<int>("state_publish_period_ms", 200);
    if (state_publish_period_ms <= 0) {
      throw std::invalid_argument("state_publish_period_ms must be positive");
    }
    authority_pub_ = create_publisher<MotionAuthorityMsg>(
      "/motion/authority", rclcpp::QoS(1).reliable().transient_local());
    safety_event_pub_ = create_publisher<SafetyEvent>("/safety/event", 10);
    request_service_ = create_service<AuthorityRequest>(
      "/motion/authority/request",
      std::bind(&MotionInterlockManagerNode::on_request, this,
        std::placeholders::_1, std::placeholders::_2));
    stopped_sub_ = create_subscription<MotionStopped>(
      "/motion/stopped", 20,
      std::bind(&MotionInterlockManagerNode::on_stopped, this, std::placeholders::_1));
    timer_ = create_wall_timer(50ms, std::bind(&MotionInterlockManagerNode::tick, this));
    state_publish_timer_ = create_wall_timer(
      std::chrono::milliseconds(state_publish_period_ms),
      std::bind(&MotionInterlockManagerNode::publish, this));
    publish();
  }

private:
  MotionAuthorityMsg make_state() const {
    MotionAuthorityMsg message;
    message.stamp = now();
    message.sequence = sequence_;
    message.state = wire_authority(interlock_.state());
    message.pending = wire_authority(interlock_.pending());
    message.owner = owner_;
    message.reason_code = reason_code_;
    message.detail = detail_;
    return message;
  }

  void remember(const std::string & id, const AuthorityRequest::Response & response) {
    request_cache_[id] = response;
    request_order_.push_back(id);
    if (request_order_.size() > 128) {
      request_cache_.erase(request_order_.front());
      request_order_.pop_front();
    }
  }

  void on_request(const AuthorityRequest::Request::SharedPtr request,
                  AuthorityRequest::Response::SharedPtr response) {
    if (request->request_id.empty()) {
      response->accepted = false;
      response->reason_code = "MOTION_REQUEST_ID_REQUIRED";
      response->detail = "request_id is required";
      response->authority = make_state();
      return;
    }
    if (const auto it = request_cache_.find(request->request_id);
        it != request_cache_.end()) {
      *response = it->second;
      return;
    }

    using Request = motion_interlock_manager::Request;
    motion_interlock_manager::Transition transition{
      interlock_.state(), interlock_.state(), false, "unknown authority operation"};
    if (request->operation == AuthorityRequest::Request::REQUEST_BASE) {
      transition = interlock_.request(Request::Base);
    } else if (request->operation == AuthorityRequest::Request::REQUEST_ARM) {
      transition = interlock_.request(Request::Arm);
    } else if (request->operation == AuthorityRequest::Request::RELEASE) {
      transition = interlock_.request(Request::Release);
    }

    if (transition.accepted) {
      if (request->operation != AuthorityRequest::Request::RELEASE) {
        owner_ = request->requester;
      }
      accept_transition(transition, "MOTION_AUTHORITY_CHANGED");
    }
    response->accepted = transition.accepted;
    response->authority = make_state();
    response->reason_code = transition.accepted ? reason_code_ : "MOTION_AUTHORITY_REJECTED";
    response->detail = transition.reason;
    remember(request->request_id, *response);
  }

  void on_stopped(const MotionStopped::SharedPtr message) {
    if (!message->stopped) return;
    const auto age = now() - rclcpp::Time(message->stamp);
    const auto timeout_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
      stopped_feedback_timeout_).count();
    if (age.nanoseconds() < 0 || age.nanoseconds() > timeout_ns) {
      RCLCPP_WARN(get_logger(), "Rejected stale or future motion-stopped feedback");
      return;
    }
    motion_interlock_manager::Transition transition{
      interlock_.state(), interlock_.state(), false, "unsupported resource"};
    if (message->resource == MotionStopped::BASE) {
      transition = interlock_.base_stopped();
    } else if (message->resource == MotionStopped::ARM) {
      transition = interlock_.arm_stopped();
    }
    if (transition.accepted) accept_transition(transition, "MOTION_STOP_CONFIRMED");
  }

  void tick() {
    if (!transition_started_) return;
    if (std::chrono::steady_clock::now() - *transition_started_ <= transition_timeout_) return;
    const auto transition = interlock_.transition_timeout();
    transition_started_.reset();
    if (!transition.accepted) return;
    owner_.clear();
    accept_transition(transition, "MOTION_AUTHORITY_TIMEOUT");

    SafetyEvent event;
    event.stamp = now();
    event.sequence = ++safety_event_sequence_;
    event.event = SafetyEvent::HEALTH_FAULT;
    event.source = "motion_interlock_manager";
    event.reason_code = "SAFETY_MOTION_STOP_TIMEOUT";
    event.detail = "motion authority transition timed out";
    safety_event_pub_->publish(event);
  }

  void accept_transition(const motion_interlock_manager::Transition & transition,
                         const std::string & reason_code) {
    reason_code_ = reason_code;
    detail_ = transition.reason;
    ++sequence_;
    const auto state = interlock_.state();
    if (state == CoreAuthority::BaseStopping || state == CoreAuthority::ArmStopping) {
      transition_started_ = std::chrono::steady_clock::now();
    } else {
      transition_started_.reset();
      if (state == CoreAuthority::None) owner_.clear();
    }
    RCLCPP_INFO(get_logger(), "Motion authority %s -> %s: %s",
      motion_interlock_manager::to_string(transition.from),
      motion_interlock_manager::to_string(transition.to), transition.reason);
    publish();
  }

  void publish() { authority_pub_->publish(make_state()); }

  motion_interlock_manager::MotionInterlock interlock_;
  uint64_t sequence_{0};
  uint64_t safety_event_sequence_{0};
  std::string owner_;
  std::string reason_code_{"MOTION_AUTHORITY_INITIALIZED"};
  std::string detail_{"no authority holder"};
  std::chrono::milliseconds transition_timeout_{1000};
  std::chrono::milliseconds stopped_feedback_timeout_{500};
  std::optional<std::chrono::steady_clock::time_point> transition_started_;
  std::unordered_map<std::string, AuthorityRequest::Response> request_cache_;
  std::deque<std::string> request_order_;
  rclcpp::Publisher<MotionAuthorityMsg>::SharedPtr authority_pub_;
  rclcpp::Publisher<SafetyEvent>::SharedPtr safety_event_pub_;
  rclcpp::Service<AuthorityRequest>::SharedPtr request_service_;
  rclcpp::Subscription<MotionStopped>::SharedPtr stopped_sub_;
  rclcpp::TimerBase::SharedPtr timer_;
  rclcpp::TimerBase::SharedPtr state_publish_timer_;
};

}  // namespace

int main(int argc, char ** argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<MotionInterlockManagerNode>());
  rclcpp::shutdown();
  return 0;
}
