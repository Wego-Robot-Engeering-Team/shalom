// Copyright (c) 2026 WeGo Robotics. All rights reserved.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "geometry_msgs/msg/twist.hpp"
#include "nav2_msgs/action/navigate_to_pose.hpp"
#include "nav_msgs/msg/odometry.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"
#include "shalom_interfaces/msg/mission_plan.hpp"
#include "shalom_interfaces/msg/mission_state.hpp"
#include "shalom_interfaces/msg/motion_authority.hpp"
#include "shalom_interfaces/msg/motion_stopped.hpp"
#include "shalom_interfaces/msg/safety_state.hpp"
#include "shalom_interfaces/srv/authority_request.hpp"
#include "shalom_interfaces/srv/configure_mission.hpp"
#include "shalom_interfaces/srv/mission_control.hpp"
#include "shalom_interfaces/srv/safety_command.hpp"

#include "mission_manager/bt/nav2_bt.hpp"
#include "mission_manager/mission_state_machine.hpp"
#include "mission_manager/operation_registry.hpp"

namespace {

using namespace std::chrono_literals;
using ConfigureMission = shalom_interfaces::srv::ConfigureMission;
using MissionControl = shalom_interfaces::srv::MissionControl;
using MissionPlan = shalom_interfaces::msg::MissionPlan;
using MissionState = shalom_interfaces::msg::MissionState;
using MissionWaypoint = shalom_interfaces::msg::MissionWaypoint;
using MotionAuthority = shalom_interfaces::msg::MotionAuthority;
using MotionStopped = shalom_interfaces::msg::MotionStopped;
using SafetyState = shalom_interfaces::msg::SafetyState;
using AuthorityRequest = shalom_interfaces::srv::AuthorityRequest;
using SafetyCommand = shalom_interfaces::srv::SafetyCommand;
using NavigateToPose = nav2_msgs::action::NavigateToPose;
using NavGoalHandle = rclcpp_action::ClientGoalHandle<NavigateToPose>;

uint8_t wire_state(mission_manager::core::State state) {
  using State = mission_manager::core::State;
  switch (state) {
    case State::Idle: return MissionState::IDLE;
    case State::Ready: return MissionState::READY;
    case State::Running: return MissionState::RUNNING;
    case State::Pausing: return MissionState::PAUSING;
    case State::Paused: return MissionState::PAUSED;
    case State::Recovering: return MissionState::RECOVERING;
    case State::Returning: return MissionState::RETURNING;
    case State::Completed: return MissionState::COMPLETED;
    case State::Failed: return MissionState::FAILED;
  }
  return MissionState::FAILED;
}

bool finite_pose(const geometry_msgs::msg::PoseStamped & pose) {
  const auto & p = pose.pose.position;
  const auto & q = pose.pose.orientation;
  return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
         std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) &&
         std::isfinite(q.w) && (q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w) > 1e-9;
}

template<typename Response>
void remember_response(const std::string & id, const Response & response,
                       std::unordered_map<std::string, Response> & cache,
                       std::deque<std::string> & order) {
  cache[id] = response;
  order.push_back(id);
  if (order.size() > 128) {
    cache.erase(order.front());
    order.pop_front();
  }
}

class MissionManagerNode final : public rclcpp::Node,
                                 public mission_manager::bt::Nav2Runtime {
public:
  MissionManagerNode() : Node("mission_manager") {
    map_frame_ = declare_parameter<std::string>("map_frame", "map");
    odometry_topic_ = declare_parameter<std::string>("odometry_topic", "kiss/odometry");
    safe_command_topic_ = declare_parameter<std::string>("safe_command_topic", "/cmd_vel");
    stopped_feedback_timeout_ = std::chrono::milliseconds(
      declare_parameter<int>("stopped_feedback_timeout_ms", 500));
    for (const auto & capability : declare_parameter<std::vector<std::string>>(
           "available_capabilities", std::vector<std::string>{})) {
      available_capabilities_.insert(capability);
    }

    state_pub_ = create_publisher<MissionState>(
      "/mission/state", rclcpp::QoS(1).reliable().transient_local());
    configure_service_ = create_service<ConfigureMission>(
      "/mission/configure",
      std::bind(&MissionManagerNode::on_configure, this,
        std::placeholders::_1, std::placeholders::_2));
    control_service_ = create_service<MissionControl>(
      "/mission/control",
      std::bind(&MissionManagerNode::on_control, this,
        std::placeholders::_1, std::placeholders::_2));

    safety_sub_ = create_subscription<SafetyState>(
      "/safety/state", rclcpp::QoS(1).reliable().transient_local(),
      std::bind(&MissionManagerNode::on_safety, this, std::placeholders::_1));
    authority_sub_ = create_subscription<MotionAuthority>(
      "/motion/authority", rclcpp::QoS(1).reliable().transient_local(),
      std::bind(&MissionManagerNode::on_authority, this, std::placeholders::_1));
    odometry_sub_ = create_subscription<nav_msgs::msg::Odometry>(
      odometry_topic_, 20,
      std::bind(&MissionManagerNode::on_odometry, this, std::placeholders::_1));
    safe_command_sub_ = create_subscription<geometry_msgs::msg::Twist>(
      safe_command_topic_, 20,
      std::bind(&MissionManagerNode::on_safe_command, this, std::placeholders::_1));
    stopped_sub_ = create_subscription<MotionStopped>(
      "/motion/stopped", 20,
      std::bind(&MissionManagerNode::on_motion_stopped, this, std::placeholders::_1));

    safety_client_ = create_client<SafetyCommand>("/safety/command");
    authority_client_ = create_client<AuthorityRequest>("/motion/authority/request");
    nav_client_ = rclcpp_action::create_client<NavigateToPose>(this, "navigate_to_pose");
    register_operation_executors();
    timer_ = create_wall_timer(100ms, std::bind(&MissionManagerNode::tick, this));
    publish_state();
  }

private:
  MissionState make_state() const {
    MissionState message;
    message.stamp = now();
    message.sequence = sequence_;
    message.mission_id = has_plan_ ? plan_.mission_id : "";
    message.state = wire_state(fsm_.state());
    const auto state = fsm_.state();
    const bool has_current = has_plan_ && mission_index_ < plan_.waypoints.size() &&
      (state == mission_manager::core::State::Running ||
       state == mission_manager::core::State::Pausing ||
       state == mission_manager::core::State::Paused ||
       state == mission_manager::core::State::Recovering ||
       state == mission_manager::core::State::Failed);
    message.current_step = has_current ? static_cast<int32_t>(mission_index_) : -1;
    message.total_steps = has_plan_ ? static_cast<uint32_t>(plan_.waypoints.size()) : 0;
    message.return_to_dock = has_plan_ && plan_.return_to_dock;
    message.reason_code = reason_code_;
    message.detail = detail_;
    return message;
  }

  bool has_capability(const std::string & capability) const {
    return available_capabilities_.find(capability) != available_capabilities_.end();
  }

  void register_operation_executors() {
    const bool added = operation_registry_.add(
      MissionWaypoint::NAVIGATE_ONLY,
      {
        "navigate",
        {"navigation"},
        [this](const std::string & goal_id) {
          const auto status = nav_bt_.tick(*this, goal_id);
          if (status != mission_manager::bt::Status::Failure) {
            return mission_manager::execution::OperationRegistry::Result{status};
          }
          const bool unavailable = goal_unsendable_;
          goal_unsendable_ = false;
          return mission_manager::execution::OperationRegistry::Result{
            status,
            unavailable
              ? mission_manager::execution::OperationRegistry::FailurePolicy::Pause
              : mission_manager::execution::OperationRegistry::FailurePolicy::Fail,
            unavailable ? "MISSION_NAV_UNAVAILABLE" : "MISSION_NAV_FAILED"};
        },
        [this]() { nav_bt_.halt(*this); },
      });
    if (!added) {
      throw std::logic_error("failed to register NAVIGATE_ONLY executor");
    }
  }

  bool executor_is_available(
      const mission_manager::execution::OperationRegistry::Executor & executor,
      std::string & missing_capability) const {
    for (const auto & capability : executor.required_capabilities) {
      if (!has_capability(capability)) {
        missing_capability = capability;
        return false;
      }
    }
    return true;
  }

  bool validate_operation(uint8_t operation, const std::string & context,
                          std::string & code, std::string & detail) const {
    const auto * executor = operation_registry_.find(operation);
    if (executor == nullptr) {
      code = "MISSION_OPERATION_UNAVAILABLE";
      detail = "no executor is registered for " + context + " operation " +
        std::to_string(operation);
      return false;
    }
    std::string missing_capability;
    if (!executor_is_available(*executor, missing_capability)) {
      code = "MISSION_CAPABILITY_UNAVAILABLE";
      detail = context + " operation " + executor->name +
        " requires unavailable capability: " + missing_capability;
      return false;
    }
    return true;
  }

  bool validate_plan(const MissionPlan & plan, std::string & code, std::string & detail) const {
    if (plan.mission_id.empty() || plan.map_id.empty()) {
      code = "MISSION_PLAN_INVALID";
      detail = "mission_id and map_id are required";
      return false;
    }
    if (plan.waypoints.empty()) {
      code = "MISSION_PLAN_EMPTY";
      detail = "at least one waypoint is required";
      return false;
    }
    for (const auto & capability : plan.required_capabilities) {
      if (!has_capability(capability)) {
        code = "MISSION_CAPABILITY_UNAVAILABLE";
        detail = "required capability is unavailable: " + capability;
        return false;
      }
    }
    std::unordered_set<std::string> ids;
    for (const auto & waypoint : plan.waypoints) {
      if (waypoint.waypoint_id.empty() || !ids.insert(waypoint.waypoint_id).second) {
        code = "MISSION_WAYPOINT_INVALID";
        detail = "waypoint IDs must be non-empty and unique";
        return false;
      }
      if (!finite_pose(waypoint.target_pose)) {
        code = "MISSION_WAYPOINT_INVALID";
        detail = "waypoint pose is not finite or has an invalid orientation";
        return false;
      }
      if (!validate_operation(waypoint.operation, "waypoint", code, detail)) return false;
      for (const auto & capability : waypoint.required_capabilities) {
        if (!has_capability(capability)) {
          code = "MISSION_CAPABILITY_UNAVAILABLE";
          detail = "waypoint capability is unavailable: " + capability;
          return false;
        }
      }
    }
    if (plan.return_to_dock) {
      if (!plan.has_dock_approach || !finite_pose(plan.dock_approach.target_pose)) {
        code = "MISSION_DOCK_APPROACH_REQUIRED";
        detail = "return_to_dock requires a valid dock approach pose";
        return false;
      }
      if (plan.dock_approach.operation != MissionWaypoint::NAVIGATE_ONLY) {
        code = "MISSION_DOCK_APPROACH_INVALID";
        detail = "dock approach must use the navigation operation";
        return false;
      }
      if (!validate_operation(plan.dock_approach.operation, "dock approach", code, detail)) {
        return false;
      }
      for (const auto & capability : plan.dock_approach.required_capabilities) {
        if (!has_capability(capability)) {
          code = "MISSION_CAPABILITY_UNAVAILABLE";
          detail = "dock approach capability is unavailable: " + capability;
          return false;
        }
      }
    }
    return true;
  }

  void clear_pending_start() {
    start_requested_ = false;
    last_dependency_request_.reset();
  }

  void on_configure(const ConfigureMission::Request::SharedPtr request,
                    ConfigureMission::Response::SharedPtr response) {
    if (request->request_id.empty()) {
      response->accepted = false;
      response->reason_code = "MISSION_REQUEST_ID_REQUIRED";
      response->detail = "request_id is required";
      response->state = make_state();
      return;
    }
    if (const auto it = configure_cache_.find(request->request_id);
        it != configure_cache_.end()) {
      *response = it->second;
      return;
    }

    std::string code;
    std::string detail;
    const auto state = fsm_.state();
    const bool replaceable = state == mission_manager::core::State::Idle ||
      state == mission_manager::core::State::Ready ||
      state == mission_manager::core::State::Completed ||
      state == mission_manager::core::State::Failed;
    if (!replaceable) {
      code = "MISSION_BUSY";
      detail = "an active or paused mission cannot be replaced";
    } else if (!validate_plan(request->plan, code, detail)) {
      // validate_plan supplies the reason.
    } else {
      // A START authorizes only the plan that was current when it was received.
      // A replacement plan always requires a new explicit START.
      clear_pending_start();
      if (state == mission_manager::core::State::Ready) {
        fsm_.dispatch(mission_manager::core::Event::StopRequested);
      } else if (state == mission_manager::core::State::Completed ||
                 state == mission_manager::core::State::Failed) {
        fsm_.dispatch(mission_manager::core::Event::ResetRequested);
      }
      plan_ = request->plan;
      for (auto & waypoint : plan_.waypoints) {
        if (waypoint.target_pose.header.frame_id.empty()) waypoint.target_pose.header.frame_id = map_frame_;
      }
      if (plan_.has_dock_approach && plan_.dock_approach.target_pose.header.frame_id.empty()) {
        plan_.dock_approach.target_pose.header.frame_id = map_frame_;
      }
      has_plan_ = true;
      mission_index_ = 0;
      fsm_.set_return_to_dock(plan_.return_to_dock);
      const auto transition = fsm_.dispatch(mission_manager::core::Event::MissionConfigured);
      if (transition.accepted) {
        reason_code_ = "MISSION_CONFIGURED";
        detail_ = transition.reason;
        ++sequence_;
        publish_state();
        response->accepted = true;
      } else {
        code = "MISSION_CONFIGURE_REJECTED";
        detail = transition.reason;
      }
    }
    response->state = make_state();
    response->reason_code = response->accepted ? reason_code_ : code;
    response->detail = response->accepted ? detail_ : detail;
    remember_response(request->request_id, *response, configure_cache_, configure_order_);
  }

  void on_control(const MissionControl::Request::SharedPtr request,
                  MissionControl::Response::SharedPtr response) {
    if (request->request_id.empty()) {
      response->accepted = false;
      response->reason_code = "MISSION_REQUEST_ID_REQUIRED";
      response->detail = "request_id is required";
      response->state = make_state();
      return;
    }
    if (const auto it = control_cache_.find(request->request_id);
        it != control_cache_.end()) {
      *response = it->second;
      return;
    }
    if (!request->mission_id.empty() && has_plan_ && request->mission_id != plan_.mission_id) {
      response->accepted = false;
      response->reason_code = "MISSION_ID_MISMATCH";
      response->detail = "command mission_id does not match the configured mission";
      response->state = make_state();
      remember_response(request->request_id, *response, control_cache_, control_order_);
      return;
    }

    using Event = mission_manager::core::Event;
    bool accepted = false;
    std::string rejected_detail;
    if (request->operation == MissionControl::Request::START) {
      if (!has_plan_ || fsm_.state() != mission_manager::core::State::Ready) {
        rejected_detail = "mission is not ready";
      } else {
        start_requested_ = true;
        reason_code_ = "MISSION_START_REQUESTED";
        detail_ = "waiting for safety, authority, and fresh odometry";
        accepted = true;
        request_dependencies();
        publish_state();
      }
    } else if (request->operation == MissionControl::Request::PAUSE) {
      accepted = dispatch(Event::PauseRequested, "MISSION_PAUSE_REQUESTED");
      if (!accepted) rejected_detail = detail_;
    } else if (request->operation == MissionControl::Request::RESUME) {
      accepted = dispatch(Event::ResumeRequested, "MISSION_RESUME_REQUESTED");
      if (accepted) request_dependencies();
      else rejected_detail = detail_;
    } else if (request->operation == MissionControl::Request::STOP) {
      const auto state = fsm_.state();
      accepted = dispatch(
        state == mission_manager::core::State::Completed ||
        state == mission_manager::core::State::Failed ? Event::ResetRequested : Event::StopRequested,
        "MISSION_STOP_REQUESTED");
      if (!accepted) rejected_detail = detail_;
    } else if (request->operation == MissionControl::Request::RESET) {
      accepted = dispatch(Event::ResetRequested, "MISSION_RESET_REQUESTED");
      if (!accepted) rejected_detail = detail_;
    } else {
      rejected_detail = "unknown mission control operation";
    }

    response->accepted = accepted;
    response->state = make_state();
    response->reason_code = accepted ? reason_code_ : "MISSION_CONTROL_REJECTED";
    response->detail = accepted ? detail_ : rejected_detail;
    remember_response(request->request_id, *response, control_cache_, control_order_);
  }

  void on_safety(const SafetyState::SharedPtr message) {
    safety_state_ = message->state;
    have_safety_state_ = true;
    using State = mission_manager::core::State;
    const auto state = fsm_.state();
    if (state == State::Ready && start_requested_ &&
        (message->reason_code == "SAFETY_COMM_TIMEOUT_STOP" ||
         message->reason_code == "SAFETY_WATCHDOG_TIMEOUT" ||
         message->state == SafetyState::E_STOP_LATCHED ||
         message->state == SafetyState::FAULT)) {
      clear_pending_start();
      reason_code_ = "MISSION_START_CANCELLED_BY_SAFETY";
      detail_ = "a new operator start is required after the safety stop";
      ++sequence_;
      publish_state();
    }
    if (message->state != SafetyState::NORMAL &&
        (state == State::Running || state == State::Returning || state == State::Recovering)) {
      dispatch(mission_manager::core::Event::SafetyStop, "MISSION_SAFETY_STOP");
    }
  }

  void on_authority(const MotionAuthority::SharedPtr message) {
    authority_state_ = message->state;
    have_authority_ = true;
  }

  void on_odometry(const nav_msgs::msg::Odometry::SharedPtr message) {
    (void)message;
    last_odometry_ = std::chrono::steady_clock::now();
  }

  void on_safe_command(const geometry_msgs::msg::Twist::SharedPtr message) {
    constexpr double epsilon = 1e-6;
    safe_command_zero_ = std::abs(message->linear.x) <= epsilon &&
      std::abs(message->linear.y) <= epsilon && std::abs(message->linear.z) <= epsilon &&
      std::abs(message->angular.x) <= epsilon && std::abs(message->angular.y) <= epsilon &&
      std::abs(message->angular.z) <= epsilon;
    last_safe_command_ = std::chrono::steady_clock::now();
  }

  void on_motion_stopped(const MotionStopped::SharedPtr message) {
    if (message->resource != MotionStopped::BASE) return;
    base_stopped_ = message->stopped;
    last_stopped_feedback_ = std::chrono::steady_clock::now();
  }

  bool motion_dependencies_ready() const {
    const bool odometry_fresh = last_odometry_.has_value() &&
      std::chrono::steady_clock::now() - *last_odometry_ <= 1s;
    return have_safety_state_ && safety_state_ == SafetyState::NORMAL &&
      have_authority_ && authority_state_ == MotionAuthority::BASE_ACTIVE && odometry_fresh;
  }

  bool motion_quiesced() const {
    const auto now_steady = std::chrono::steady_clock::now();
    const bool command_fresh = last_safe_command_.has_value() &&
      now_steady - *last_safe_command_ <= 250ms;
    const bool stopped_feedback_fresh = last_stopped_feedback_.has_value() &&
      now_steady - *last_stopped_feedback_ <= stopped_feedback_timeout_;
    return goal_phase_ != GoalPhase::Active && !nav_goal_ && command_fresh &&
      safe_command_zero_ && stopped_feedback_fresh && base_stopped_;
  }

  bool dispatch(mission_manager::core::Event event, const std::string & reason_code) {
    const auto transition = fsm_.dispatch(event);
    detail_ = transition.reason;
    if (!transition.accepted) return false;
    reason_code_ = reason_code;
    ++sequence_;
    if (transition.to == mission_manager::core::State::Pausing) {
      if (active_operation_.has_value()) {
        if (const auto * executor = operation_registry_.find(*active_operation_)) executor->halt();
        active_operation_.reset();
      }
    }
    if (event == mission_manager::core::Event::StartRequested) mission_index_ = 0;
    if (transition.to == mission_manager::core::State::Idle) {
      has_plan_ = false;
      plan_ = MissionPlan{};
      mission_index_ = 0;
      clear_pending_start();
    }
    RCLCPP_INFO(get_logger(), "Mission %s -> %s: %s",
      mission_manager::core::to_string(transition.from),
      mission_manager::core::to_string(transition.to), transition.reason);
    publish_state();
    return true;
  }

  void request_dependencies() {
    const auto now_steady = std::chrono::steady_clock::now();
    if (last_dependency_request_.has_value() && now_steady - *last_dependency_request_ < 500ms) {
      return;
    }
    last_dependency_request_ = now_steady;

    if (safety_client_->service_is_ready()) {
      auto request = std::make_shared<SafetyCommand::Request>();
      request->request_id = "mission-safety-" + std::to_string(++dependency_sequence_);
      request->operator_id = "mission_manager";
      request->operation = SafetyCommand::Request::RESUME;
      safety_client_->async_send_request(request,
        [this](rclcpp::Client<SafetyCommand>::SharedFuture future) {
          const auto response = future.get();
          if (!response->accepted && response->state.state != SafetyState::NORMAL) {
            RCLCPP_WARN(get_logger(), "Safety resume rejected: %s", response->detail.c_str());
          }
        });
    }
    if (authority_client_->service_is_ready()) {
      auto request = std::make_shared<AuthorityRequest::Request>();
      request->request_id = "mission-authority-" + std::to_string(++dependency_sequence_);
      request->requester = "mission_manager";
      request->operation = AuthorityRequest::Request::REQUEST_BASE;
      authority_client_->async_send_request(request,
        [this](rclcpp::Client<AuthorityRequest>::SharedFuture future) {
          const auto response = future.get();
          if (!response->accepted && response->authority.state != MotionAuthority::BASE_ACTIVE) {
            RCLCPP_WARN(get_logger(), "Base authority rejected: %s", response->detail.c_str());
          }
        });
    }
  }

  void tick() {
    using Event = mission_manager::core::Event;
    using State = mission_manager::core::State;
    using Status = mission_manager::bt::Status;
    const auto state = fsm_.state();
    if (state == State::Ready && start_requested_) {
      if (motion_dependencies_ready()) {
        clear_pending_start();
        dispatch(Event::StartRequested, "MISSION_STARTED");
      } else {
        request_dependencies();
      }
      return;
    }
    if (state == State::Pausing) {
      if (motion_quiesced()) dispatch(Event::MotionQuiesced, "MISSION_MOTION_QUIESCED");
      return;
    }
    if (state == State::Recovering) {
      if (motion_dependencies_ready()) dispatch(Event::RecoveryReady, "MISSION_RECOVERY_READY");
      else request_dependencies();
      return;
    }
    if (state != State::Running && state != State::Returning) return;

    const bool returning = state == State::Returning;
    if (!returning && mission_index_ >= plan_.waypoints.size()) {
      dispatch(Event::InspectionComplete, "MISSION_INSPECTION_COMPLETE");
      return;
    }
    const std::string goal_id = returning ? "dock" : std::to_string(mission_index_);
    const auto operation = returning
      ? plan_.dock_approach.operation : plan_.waypoints[mission_index_].operation;
    const auto * executor = operation_registry_.find(operation);
    if (executor == nullptr) {
      dispatch(Event::FatalStepFailure, "MISSION_OPERATION_UNAVAILABLE");
      return;
    }
    active_operation_ = operation;
    const auto result = executor->tick(goal_id);
    if (result.status == Status::Running) return;
    if (result.status == Status::Failure) {
      dispatch(
        result.failure_policy ==
          mission_manager::execution::OperationRegistry::FailurePolicy::Pause
          ? Event::PauseRequested : Event::FatalStepFailure,
        result.reason_code.empty() ? "MISSION_STEP_FAILED" : result.reason_code);
      return;
    }
    active_operation_.reset();
    if (returning) {
      dispatch(Event::ReturnComplete, "MISSION_RETURN_COMPLETE");
      return;
    }
    ++mission_index_;
    ++sequence_;
    publish_state();
    if (mission_index_ >= plan_.waypoints.size()) {
      dispatch(Event::InspectionComplete, "MISSION_INSPECTION_COMPLETE");
    }
  }

  mission_manager::bt::Status navigate_to(const std::string & goal_id) override {
    using Status = mission_manager::bt::Status;
    switch (goal_phase_) {
      case GoalPhase::Active: return Status::Running;
      case GoalPhase::Succeeded: goal_phase_ = GoalPhase::Idle; return Status::Success;
      case GoalPhase::Failed: goal_phase_ = GoalPhase::Idle; return Status::Failure;
      case GoalPhase::Idle: break;
    }

    const geometry_msgs::msg::PoseStamped * pose = nullptr;
    if (goal_id == "dock") {
      if (plan_.has_dock_approach) pose = &plan_.dock_approach.target_pose;
    } else if (mission_index_ < plan_.waypoints.size()) {
      pose = &plan_.waypoints[mission_index_].target_pose;
    }
    if (pose == nullptr || !nav_client_->action_server_is_ready()) {
      goal_unsendable_ = true;
      return Status::Failure;
    }

    NavigateToPose::Goal goal;
    goal.pose = *pose;
    goal.pose.header.stamp = now();
    if (goal.pose.header.frame_id.empty()) goal.pose.header.frame_id = map_frame_;
    const uint64_t generation = ++goal_generation_;
    goal_phase_ = GoalPhase::Active;
    rclcpp_action::Client<NavigateToPose>::SendGoalOptions options;
    options.goal_response_callback = [this, generation](NavGoalHandle::SharedPtr handle) {
      if (generation != goal_generation_) {
        if (handle) nav_client_->async_cancel_goal(handle);
        return;
      }
      if (!handle) {
        goal_unsendable_ = true;
        goal_phase_ = GoalPhase::Failed;
        return;
      }
      nav_goal_ = handle;
    };
    options.result_callback = [this, generation](const NavGoalHandle::WrappedResult & result) {
      if (generation != goal_generation_) return;
      nav_goal_.reset();
      goal_phase_ = result.code == rclcpp_action::ResultCode::SUCCEEDED
        ? GoalPhase::Succeeded : GoalPhase::Failed;
    };
    nav_client_->async_send_goal(goal, options);
    return Status::Running;
  }

  void cancel_navigation() override {
    ++goal_generation_;
    if (nav_goal_) nav_client_->async_cancel_goal(nav_goal_);
    nav_goal_.reset();
    goal_phase_ = GoalPhase::Idle;
  }

  void publish_state() { state_pub_->publish(make_state()); }

  enum class GoalPhase { Idle, Active, Succeeded, Failed };

  mission_manager::core::StateMachine fsm_;
  mission_manager::bt::Nav2Bt nav_bt_;
  mission_manager::execution::OperationRegistry operation_registry_;
  MissionPlan plan_;
  bool has_plan_{false};
  bool start_requested_{false};
  std::size_t mission_index_{0};
  uint64_t sequence_{0};
  uint64_t dependency_sequence_{0};
  uint64_t goal_generation_{0};
  std::string reason_code_{"MISSION_IDLE"};
  std::string detail_{"waiting for mission configuration"};
  std::string map_frame_{"map"};
  std::string odometry_topic_;
  std::string safe_command_topic_;
  std::unordered_set<std::string> available_capabilities_;
  std::chrono::milliseconds stopped_feedback_timeout_{500};
  bool have_safety_state_{false};
  bool have_authority_{false};
  uint8_t safety_state_{SafetyState::INITIALIZING};
  uint8_t authority_state_{MotionAuthority::NONE};
  bool base_stopped_{false};
  bool safe_command_zero_{true};
  std::optional<std::chrono::steady_clock::time_point> last_odometry_;
  std::optional<std::chrono::steady_clock::time_point> last_safe_command_;
  std::optional<std::chrono::steady_clock::time_point> last_stopped_feedback_;
  std::optional<std::chrono::steady_clock::time_point> last_dependency_request_;
  GoalPhase goal_phase_{GoalPhase::Idle};
  bool goal_unsendable_{false};
  std::optional<uint8_t> active_operation_;
  NavGoalHandle::SharedPtr nav_goal_;

  std::unordered_map<std::string, ConfigureMission::Response> configure_cache_;
  std::deque<std::string> configure_order_;
  std::unordered_map<std::string, MissionControl::Response> control_cache_;
  std::deque<std::string> control_order_;

  rclcpp::Publisher<MissionState>::SharedPtr state_pub_;
  rclcpp::Service<ConfigureMission>::SharedPtr configure_service_;
  rclcpp::Service<MissionControl>::SharedPtr control_service_;
  rclcpp::Subscription<SafetyState>::SharedPtr safety_sub_;
  rclcpp::Subscription<MotionAuthority>::SharedPtr authority_sub_;
  rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr odometry_sub_;
  rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr safe_command_sub_;
  rclcpp::Subscription<MotionStopped>::SharedPtr stopped_sub_;
  rclcpp::Client<SafetyCommand>::SharedPtr safety_client_;
  rclcpp::Client<AuthorityRequest>::SharedPtr authority_client_;
  rclcpp_action::Client<NavigateToPose>::SharedPtr nav_client_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace

int main(int argc, char ** argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<MissionManagerNode>());
  rclcpp::shutdown();
  return 0;
}
