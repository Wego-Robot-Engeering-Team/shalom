// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include "action_msgs/srv/cancel_goal.hpp"
#include "geometry_msgs/msg/twist.hpp"
#include "nav2_msgs/action/dock_robot.hpp"
#include "nav2_msgs/action/navigate_to_pose.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"
#include "shalom_interfaces/msg/base_source_state.hpp"
#include "shalom_interfaces/msg/motion_authority.hpp"
#include "shalom_interfaces/msg/motion_stopped.hpp"
#include "shalom_interfaces/msg/safety_state.hpp"
#include "shalom_interfaces/srv/select_base_source.hpp"
#include "std_msgs/msg/bool.hpp"

namespace {

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
using BaseSourceState = shalom_interfaces::msg::BaseSourceState;
using MotionStopped = shalom_interfaces::msg::MotionStopped;
using MotionAuthority = shalom_interfaces::msg::MotionAuthority;
using SafetyState = shalom_interfaces::msg::SafetyState;
using SelectBaseSource = shalom_interfaces::srv::SelectBaseSource;
using Twist = geometry_msgs::msg::Twist;
using Bool = std_msgs::msg::Bool;
using NavigateToPose = nav2_msgs::action::NavigateToPose;
using DockRobot = nav2_msgs::action::DockRobot;

constexpr auto kTick = 50ms;
constexpr auto kLockLease = 300ms;
constexpr auto kManualReadyLease = 150ms;
constexpr auto kSafetyLease = 250ms;
constexpr auto kAuthorityLease = 500ms;
constexpr auto kCommandLease = 300ms;
constexpr auto kFirstCommandTimeout = 2s;
constexpr auto kTransitionTimeout = 2s;
constexpr auto kStoppedFeedbackLease = 500ms;

bool valid_source(uint8_t source) {
  return source <= BaseSourceState::MISSION;
}

class BaseSourceManagerNode final : public rclcpp::Node {
public:
  BaseSourceManagerNode() : Node("base_source_manager") {
    command_pub_ = create_publisher<Twist>("/motion/autonomy/cmd_vel", 10);
    inhibit_pub_ = create_publisher<Bool>("/motion/base_source/inhibit", 10);
    state_pub_ = create_publisher<BaseSourceState>(
      "/motion/base_source/state", rclcpp::QoS(1).reliable().transient_local());

    constexpr std::array<const char *, 4> topics{
      "/motion/nav/cmd_vel", "/motion/dock/cmd_vel",
      "/motion/stair/cmd_vel", "/motion/mission/cmd_vel"};
    for (size_t i = 0; i < topics.size(); ++i) {
      const auto source = static_cast<uint8_t>(i + BaseSourceState::NAV);
      source_subs_[i] = create_subscription<Twist>(
        topics[i], 10,
        [this, source](const Twist::SharedPtr message) { on_command(source, *message); });
    }

    stopped_sub_ = create_subscription<MotionStopped>(
      "/motion/stopped", 20,
      std::bind(&BaseSourceManagerNode::on_stopped, this, std::placeholders::_1));
    manual_lock_sub_ = create_subscription<Bool>(
      "/motion/manual_autonomy_lock", 10,
      std::bind(&BaseSourceManagerNode::on_manual_lock, this, std::placeholders::_1));
    manual_ready_sub_ = create_subscription<Bool>(
      "/motion/manual_ready", 10,
      std::bind(&BaseSourceManagerNode::on_manual_ready, this, std::placeholders::_1));
    safety_sub_ = create_subscription<SafetyState>(
      "/safety/state", rclcpp::QoS(1).reliable().transient_local(),
      std::bind(&BaseSourceManagerNode::on_safety, this, std::placeholders::_1));
    authority_sub_ = create_subscription<MotionAuthority>(
      "/motion/authority", rclcpp::QoS(1).reliable().transient_local(),
      std::bind(&BaseSourceManagerNode::on_authority, this, std::placeholders::_1));

    nav_client_ = rclcpp_action::create_client<NavigateToPose>(this, "navigate_to_pose");
    dock_client_ = rclcpp_action::create_client<DockRobot>(this, "dock_robot");
    select_service_ = create_service<SelectBaseSource>(
      "/motion/base_source/select",
      std::bind(&BaseSourceManagerNode::on_select, this,
        std::placeholders::_1, std::placeholders::_2));
    timer_ = create_wall_timer(kTick, std::bind(&BaseSourceManagerNode::tick, this));

    // Inhibit and zero are published before any request can be accepted.
    publish_outputs();
    RCLCPP_INFO(get_logger(), "Autonomous base source manager started inhibited");
  }

private:
  BaseSourceState make_state() const {
    BaseSourceState state;
    state.stamp = now();
    state.sequence = sequence_;
    state.phase = phase_;
    state.active_source = active_source_;
    state.requested_source = requested_source_;
    state.owner_requester = owner_requester_;
    state.reason_code = reason_code_;
    state.detail = detail_;
    return state;
  }

  bool lock_clear(Clock::time_point steady_now) const {
    return last_lock_at_ && !manual_locked_ &&
      steady_now - *last_lock_at_ <= kLockLease;
  }

  bool stamp_recent(const builtin_interfaces::msg::Time & stamp,
                    std::chrono::nanoseconds lease) const {
    const auto age = now() - rclcpp::Time(stamp);
    return age.nanoseconds() >= 0 && age.nanoseconds() <= lease.count();
  }

  bool safety_clear(Clock::time_point steady_now) const {
    return safety_ok_ && last_safety_at_ &&
      steady_now - *last_safety_at_ <= kSafetyLease;
  }

  bool authority_clear(Clock::time_point steady_now) const {
    return authority_ok_ && last_authority_at_ &&
      steady_now - *last_authority_at_ <= kAuthorityLease;
  }

  bool autonomous_allowed(Clock::time_point steady_now) const {
    return lock_clear(steady_now) && safety_clear(steady_now) &&
      authority_clear(steady_now);
  }

  bool manual_allowed(Clock::time_point steady_now) const {
    return phase_ == BaseSourceState::INACTIVE &&
      active_source_ == BaseSourceState::NONE &&
      last_lock_at_ && manual_locked_ &&
      steady_now - *last_lock_at_ <= kLockLease &&
      last_manual_ready_at_ && manual_ready_ &&
      steady_now - *last_manual_ready_at_ <= kManualReadyLease &&
      safety_clear(steady_now) && authority_clear(steady_now);
  }

  void publish_outputs() {
    const auto steady_now = Clock::now();
    Bool inhibit;
    inhibit.data = !((phase_ == BaseSourceState::ACTIVE &&
      autonomous_allowed(steady_now)) ||
      manual_allowed(steady_now));
    inhibit_pub_->publish(inhibit);

    // The manager is the sole publisher of the admitted autonomy topic. A
    // repeated zero holds the downstream mux at zero during every handoff.
    if (phase_ == BaseSourceState::ACTIVE && !inhibit.data && last_command_at_ &&
        steady_now - *last_command_at_ <= kCommandLease) {
      command_pub_->publish(last_command_);
    } else {
      command_pub_->publish(Twist{});
    }
    state_pub_->publish(make_state());
  }

  void set_state(
    uint8_t phase, uint8_t active, uint8_t requested,
    const std::string & reason, const std::string & detail) {
    phase_ = phase;
    active_source_ = active;
    requested_source_ = requested;
    reason_code_ = reason;
    detail_ = detail;
    ++sequence_;
    last_command_at_.reset();
    publish_outputs();
  }

  void revoke(const std::string & reason, const std::string & detail) {
    ++transition_generation_;
    nav_cancel_pending_ = false;
    dock_cancel_pending_ = false;
    cancel_completed_at_.reset();
    cancel_completed_ros_at_.reset();
    stopped_confirmed_at_.reset();
    owner_requester_.clear();
    set_state(BaseSourceState::INACTIVE, BaseSourceState::NONE,
      BaseSourceState::NONE, reason, detail);
    cancel_running_actions_best_effort();
  }

  void fault(const std::string & reason, const std::string & detail) {
    ++transition_generation_;
    nav_cancel_pending_ = false;
    dock_cancel_pending_ = false;
    cancel_completed_at_.reset();
    cancel_completed_ros_at_.reset();
    stopped_confirmed_at_.reset();
    owner_requester_.clear();
    set_state(BaseSourceState::FAULT, BaseSourceState::NONE,
      requested_source_, reason, detail);
    cancel_running_actions_best_effort();
    RCLCPP_ERROR(get_logger(), "%s: %s", reason.c_str(), detail.c_str());
  }

  void cancel_running_actions_best_effort() {
    // The inhibit is already active. Cancellation also clears stale action
    // work when its original requester (for example an HMI process) died.
    // The next grant still performs strict cancellation and stop checks.
    try {
      if (nav_client_->action_server_is_ready())
        nav_client_->async_cancel_all_goals();
      if (dock_client_->action_server_is_ready())
        dock_client_->async_cancel_all_goals();
    } catch (const std::exception &error) {
      RCLCPP_WARN(get_logger(), "Best-effort action cancellation failed: %s", error.what());
    }
  }

  void on_manual_lock(const Bool::SharedPtr message) {
    last_lock_at_ = Clock::now();
    manual_locked_ = message->data;
    if (manual_locked_ && phase_ != BaseSourceState::INACTIVE) {
      revoke("BASE_SOURCE_MANUAL_LOCK", "manual lock revoked autonomous ownership");
    } else {
      publish_outputs();
    }
  }

  void on_manual_ready(const Bool::SharedPtr message) {
    last_manual_ready_at_ = Clock::now();
    manual_ready_ = message->data;
    publish_outputs();
  }

  void on_safety(const SafetyState::SharedPtr message) {
    const auto steady_now = Clock::now();
    safety_ok_ = stamp_recent(message->stamp, kSafetyLease) &&
      message->state == SafetyState::NORMAL && message->motion_permitted &&
      !message->physical_estop_active && !message->software_estop_active;
    last_safety_at_ = steady_now;
    if (!safety_ok_ && (phase_ == BaseSourceState::ACTIVE ||
                        phase_ == BaseSourceState::TRANSITIONING)) {
      revoke("BASE_SOURCE_SAFETY_REVOKED", "safety state is not fresh NORMAL with motion permitted");
    } else {
      publish_outputs();
    }
  }

  void on_authority(const MotionAuthority::SharedPtr message) {
    const auto steady_now = Clock::now();
    authority_ok_ = stamp_recent(message->stamp, kAuthorityLease) &&
      message->state == MotionAuthority::BASE_ACTIVE;
    last_authority_at_ = steady_now;
    if (!authority_ok_ && (phase_ == BaseSourceState::ACTIVE ||
                           phase_ == BaseSourceState::TRANSITIONING)) {
      revoke("BASE_SOURCE_AUTHORITY_REVOKED", "BASE_ACTIVE authority is not fresh");
    } else {
      publish_outputs();
    }
  }

  void on_command(uint8_t source, const Twist & command) {
    const auto steady_now = Clock::now();
    if (phase_ != BaseSourceState::ACTIVE || active_source_ != source ||
        !autonomous_allowed(steady_now)) {
      return;
    }
    last_command_ = command;
    last_command_at_ = steady_now;
  }

  void on_stopped(const MotionStopped::SharedPtr message) {
    if (message->resource != MotionStopped::BASE) return;
    const auto age = now() - rclcpp::Time(message->stamp);
    if (age.nanoseconds() < 0 || age.nanoseconds() >
        std::chrono::duration_cast<std::chrono::nanoseconds>(
          kStoppedFeedbackLease).count()) {
      return;
    }
    if (message->sequence <= last_stopped_sequence_) return;
    last_stopped_sequence_ = message->sequence;

    // Confirm a physical stop only with feedback produced after all cancel
    // responses, never a latched or pre-transition stopped sample.
    if (phase_ == BaseSourceState::TRANSITIONING && cancel_completed_at_ &&
        cancel_completed_ros_at_ &&
        rclcpp::Time(message->stamp) > *cancel_completed_ros_at_ &&
        message->sequence > stop_sequence_baseline_ && message->stopped) {
      stopped_confirmed_at_ = Clock::now();
    }
  }

  void mark_cancellations_complete() {
    if (phase_ == BaseSourceState::TRANSITIONING &&
        !nav_cancel_pending_ && !dock_cancel_pending_ && !cancel_completed_at_) {
      cancel_completed_at_ = Clock::now();
      cancel_completed_ros_at_ = now();
      stop_sequence_baseline_ = last_stopped_sequence_;
    }
  }

  void begin_transition(uint8_t source, const std::string & requester) {
    const auto steady_now = Clock::now();
    ++transition_generation_;
    transition_started_at_ = steady_now;
    nav_cancel_pending_ = false;
    dock_cancel_pending_ = false;
    cancel_completed_at_.reset();
    cancel_completed_ros_at_.reset();
    stopped_confirmed_at_.reset();
    stop_sequence_baseline_ = last_stopped_sequence_;
    owner_requester_ = requester;
    set_state(BaseSourceState::TRANSITIONING, BaseSourceState::NONE, source,
      "BASE_SOURCE_TRANSITION", "cancelling actions and awaiting a fresh BASE stop");

    const auto generation = transition_generation_;
    try {
      if (nav_client_->action_server_is_ready()) {
        nav_cancel_pending_ = true;
        nav_client_->async_cancel_all_goals(
          [this, generation](auto response) {
            if (generation != transition_generation_ ||
                phase_ != BaseSourceState::TRANSITIONING) return;
            nav_cancel_pending_ = false;
            if (!response || response->return_code !=
                action_msgs::srv::CancelGoal::Response::ERROR_NONE) {
              fault("BASE_SOURCE_NAV_CANCEL_FAILED", "NavigateToPose cancel was rejected");
              return;
            }
            mark_cancellations_complete();
          });
      }
      if (dock_client_->action_server_is_ready()) {
        dock_cancel_pending_ = true;
        dock_client_->async_cancel_all_goals(
          [this, generation](auto response) {
            if (generation != transition_generation_ ||
                phase_ != BaseSourceState::TRANSITIONING) return;
            dock_cancel_pending_ = false;
            if (!response || response->return_code !=
                action_msgs::srv::CancelGoal::Response::ERROR_NONE) {
              fault("BASE_SOURCE_DOCK_CANCEL_FAILED", "DockRobot cancel was rejected");
              return;
            }
            mark_cancellations_complete();
          });
      }
    } catch (const std::exception & error) {
      fault("BASE_SOURCE_CANCEL_ERROR", error.what());
      return;
    }
    mark_cancellations_complete();
  }

  void remember_request(const std::string & id, const std::string & requester,
                        uint8_t source) {
    request_records_[id] = RequestRecord{requester, source};
    request_order_.push_back(id);
    if (request_order_.size() > 128) {
      request_records_.erase(request_order_.front());
      request_order_.pop_front();
    }
  }

  void on_select(
    const SelectBaseSource::Request::SharedPtr request,
    SelectBaseSource::Response::SharedPtr response) {
    const auto steady_now = Clock::now();
    if (request->requester.empty() || request->request_id.empty()) {
      response->result_code = SelectBaseSource::Response::INVALID;
      response->detail = "requester and request_id are required";
    } else if (!valid_source(request->source)) {
      response->result_code = SelectBaseSource::Response::INVALID;
      response->detail = "unknown autonomous source";
    } else if (const auto it = request_records_.find(request->request_id);
        it != request_records_.end() &&
        (it->second.requester != request->requester ||
         it->second.source != request->source ||
         (request->source != BaseSourceState::NONE &&
          phase_ == BaseSourceState::FAULT) ||
         (request->source != BaseSourceState::NONE &&
          (requested_source_ != request->source ||
           owner_requester_ != request->requester)) ||
         (request->source == BaseSourceState::NONE &&
          phase_ != BaseSourceState::INACTIVE))) {
      response->result_code = SelectBaseSource::Response::REPLAY;
      response->detail = "request_id already belongs to a previous decision";
    } else if (request->source == BaseSourceState::NONE) {
      if (phase_ == BaseSourceState::ACTIVE ||
          phase_ == BaseSourceState::TRANSITIONING) {
        if (owner_requester_ != request->requester) {
          response->result_code = SelectBaseSource::Response::BUSY;
          response->detail = "BUSY: another requester owns the source";
          response->state = make_state();
          return;
        }
      }
      if (phase_ != BaseSourceState::INACTIVE) {
        revoke("BASE_SOURCE_RELEASED", "explicit NONE request stopped autonomy");
      }
      response->accepted = true;
      response->result_code = SelectBaseSource::Response::ACCEPTED;
      response->detail = "autonomous source released";
      if (request_records_.find(request->request_id) == request_records_.end())
        remember_request(request->request_id, request->requester, request->source);
    } else if ((phase_ == BaseSourceState::ACTIVE ||
                phase_ == BaseSourceState::TRANSITIONING) &&
               owner_requester_ != request->requester) {
      response->result_code = SelectBaseSource::Response::BUSY;
      response->detail = "BUSY: another requester owns the source";
    } else if (!lock_clear(steady_now)) {
      response->result_code = SelectBaseSource::Response::NOT_READY;
      response->detail = "manual autonomy lock is active or stale";
    } else if (!safety_clear(steady_now) || !authority_clear(steady_now)) {
      response->result_code = SelectBaseSource::Response::NOT_READY;
      response->detail = "safety NORMAL or BASE_ACTIVE authority is not fresh";
    } else if (phase_ == BaseSourceState::TRANSITIONING &&
               requested_source_ != request->source) {
      response->result_code = SelectBaseSource::Response::BUSY;
      response->detail = "another source transition is in progress";
    } else if (phase_ == BaseSourceState::ACTIVE &&
               active_source_ == request->source) {
      response->accepted = true;
      response->result_code = SelectBaseSource::Response::ACCEPTED;
      response->detail = "source already active";
      if (request_records_.find(request->request_id) == request_records_.end())
        remember_request(request->request_id, request->requester, request->source);
    } else if (phase_ == BaseSourceState::TRANSITIONING &&
               requested_source_ == request->source) {
      response->accepted = true;
      response->result_code = SelectBaseSource::Response::ACCEPTED;
      response->detail = "source transition already in progress";
      if (request_records_.find(request->request_id) == request_records_.end())
        remember_request(request->request_id, request->requester, request->source);
    } else {
      begin_transition(request->source, request->requester);
      response->accepted = phase_ == BaseSourceState::TRANSITIONING;
      response->result_code = response->accepted ? SelectBaseSource::Response::ACCEPTED
                                                 : SelectBaseSource::Response::FAILED;
      response->detail = response->accepted ? "source transition started" : detail_;
      if (response->accepted)
        remember_request(request->request_id, request->requester, request->source);
    }
    response->state = make_state();
  }

  void tick() {
    const auto steady_now = Clock::now();
    if ((!safety_clear(steady_now) || !authority_clear(steady_now)) &&
        (phase_ == BaseSourceState::ACTIVE ||
         phase_ == BaseSourceState::TRANSITIONING)) {
      revoke("BASE_SOURCE_GUARD_STALE", "safety or BASE_ACTIVE authority heartbeat was lost");
    } else if (!lock_clear(steady_now) && phase_ != BaseSourceState::INACTIVE) {
      revoke("BASE_SOURCE_MANUAL_LOCK", "manual lock is active or stale");
    } else if (phase_ == BaseSourceState::TRANSITIONING) {
      if (steady_now - transition_started_at_ > kTransitionTimeout) {
        fault("BASE_SOURCE_TRANSITION_TIMEOUT", "cancel or BASE stop was not confirmed");
      } else if (cancel_completed_at_ && stopped_confirmed_at_ &&
                 *stopped_confirmed_at_ >= *cancel_completed_at_ &&
                 steady_now - *stopped_confirmed_at_ <= kStoppedFeedbackLease) {
        active_since_ = steady_now;
        set_state(BaseSourceState::ACTIVE, requested_source_, requested_source_,
          "BASE_SOURCE_ACTIVE", "source owns autonomous base output");
      }
    } else if (phase_ == BaseSourceState::ACTIVE) {
      if ((!last_command_at_ && steady_now - active_since_ > kFirstCommandTimeout) ||
          (last_command_at_ && steady_now - *last_command_at_ > kCommandLease)) {
        fault("BASE_SOURCE_COMMAND_TIMEOUT", "selected source command lease expired");
      }
    }
    publish_outputs();
  }

  uint8_t phase_{BaseSourceState::INACTIVE};
  uint8_t active_source_{BaseSourceState::NONE};
  uint8_t requested_source_{BaseSourceState::NONE};
  uint64_t sequence_{1};
  uint64_t transition_generation_{0};
  uint64_t last_stopped_sequence_{0};
  uint64_t stop_sequence_baseline_{0};
  bool manual_locked_{true};
  bool manual_ready_{false};
  bool safety_ok_{false};
  bool authority_ok_{false};
  bool nav_cancel_pending_{false};
  bool dock_cancel_pending_{false};
  std::string reason_code_{"BASE_SOURCE_STARTUP"};
  std::string detail_{"autonomous output inhibited until an explicit selection"};
  std::string owner_requester_;
  Twist last_command_{};
  Clock::time_point transition_started_at_{};
  Clock::time_point active_since_{};
  std::optional<Clock::time_point> last_lock_at_;
  std::optional<Clock::time_point> last_manual_ready_at_;
  std::optional<Clock::time_point> last_safety_at_;
  std::optional<Clock::time_point> last_authority_at_;
  std::optional<Clock::time_point> last_command_at_;
  std::optional<Clock::time_point> cancel_completed_at_;
  std::optional<rclcpp::Time> cancel_completed_ros_at_;
  std::optional<Clock::time_point> stopped_confirmed_at_;
  struct RequestRecord {
    std::string requester;
    uint8_t source;
  };
  std::unordered_map<std::string, RequestRecord> request_records_;
  std::deque<std::string> request_order_;

  rclcpp::Publisher<Twist>::SharedPtr command_pub_;
  rclcpp::Publisher<Bool>::SharedPtr inhibit_pub_;
  rclcpp::Publisher<BaseSourceState>::SharedPtr state_pub_;
  std::array<rclcpp::Subscription<Twist>::SharedPtr, 4> source_subs_;
  rclcpp::Subscription<MotionStopped>::SharedPtr stopped_sub_;
  rclcpp::Subscription<Bool>::SharedPtr manual_lock_sub_;
  rclcpp::Subscription<Bool>::SharedPtr manual_ready_sub_;
  rclcpp::Subscription<SafetyState>::SharedPtr safety_sub_;
  rclcpp::Subscription<MotionAuthority>::SharedPtr authority_sub_;
  rclcpp_action::Client<NavigateToPose>::SharedPtr nav_client_;
  rclcpp_action::Client<DockRobot>::SharedPtr dock_client_;
  rclcpp::Service<SelectBaseSource>::SharedPtr select_service_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace

int main(int argc, char ** argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<BaseSourceManagerNode>());
  rclcpp::shutdown();
  return 0;
}
