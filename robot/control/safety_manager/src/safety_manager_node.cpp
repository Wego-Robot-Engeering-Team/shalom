// Copyright (c) 2026 WeGo Robotics. All rights reserved.

#include <chrono>
#include <deque>
#include <functional>
#include <string>
#include <unordered_map>

#include "rclcpp/rclcpp.hpp"
#include "shalom_interfaces/msg/safety_event.hpp"
#include "shalom_interfaces/msg/safety_heartbeat.hpp"
#include "shalom_interfaces/msg/safety_state.hpp"
#include "shalom_interfaces/srv/safety_command.hpp"
#include "std_msgs/msg/bool.hpp"

#include "safety_manager/safety_state_machine.hpp"

namespace {

using namespace std::chrono_literals;
using SafetyCommand = shalom_interfaces::srv::SafetyCommand;
using SafetyEvent = shalom_interfaces::msg::SafetyEvent;
using SafetyHeartbeat = shalom_interfaces::msg::SafetyHeartbeat;
using SafetyState = shalom_interfaces::msg::SafetyState;

uint8_t wire_state(safety_manager::core::State state) {
  using State = safety_manager::core::State;
  switch (state) {
    case State::Initializing: return SafetyState::INITIALIZING;
    case State::ControlledStop: return SafetyState::CONTROLLED_STOP;
    case State::Normal: return SafetyState::NORMAL;
    case State::EmergencyStopLatched: return SafetyState::E_STOP_LATCHED;
    case State::Fault: return SafetyState::FAULT;
  }
  return SafetyState::FAULT;
}

class SafetyManagerNode final : public rclcpp::Node {
public:
  SafetyManagerNode() : Node("safety_manager") {
    require_heartbeat_ = declare_parameter<bool>("require_external_heartbeat", true);
    heartbeat_timeout_ = std::chrono::milliseconds(
      declare_parameter<int>("heartbeat_timeout_ms", 750));

    state_pub_ = create_publisher<SafetyState>(
      "/safety/state", rclcpp::QoS(1).reliable().transient_local());
    command_service_ = create_service<SafetyCommand>(
      "/safety/command",
      std::bind(&SafetyManagerNode::on_command, this,
        std::placeholders::_1, std::placeholders::_2));
    event_sub_ = create_subscription<SafetyEvent>(
      "/safety/event", 20,
      std::bind(&SafetyManagerNode::on_event, this, std::placeholders::_1));
    physical_estop_sub_ = create_subscription<std_msgs::msg::Bool>(
      "/safety/physical_estop_active", 20,
      std::bind(&SafetyManagerNode::on_physical_estop, this, std::placeholders::_1));
    heartbeat_sub_ = create_subscription<SafetyHeartbeat>(
      "/safety/heartbeat", 20,
      std::bind(&SafetyManagerNode::on_heartbeat, this, std::placeholders::_1));

    last_heartbeat_ = std::chrono::steady_clock::now();
    timer_ = create_wall_timer(50ms, std::bind(&SafetyManagerNode::tick, this));
    if (!require_heartbeat_) {
      dispatch(safety_manager::core::Event::InputsReady, "startup", "SAFETY_INPUTS_READY");
    }
    publish();
  }

private:
  SafetyState make_state() const {
    SafetyState message;
    message.stamp = now();
    message.sequence = sequence_;
    message.state = wire_state(fsm_.state());
    message.motion_permitted = fsm_.motion_permitted();
    message.physical_estop_active = physical_estop_active_;
    message.software_estop_active = software_estop_active_;
    message.source = last_source_;
    message.reason_code = last_reason_code_;
    message.detail = last_detail_;
    return message;
  }

  void remember(const std::string & id, const SafetyCommand::Response & response) {
    command_cache_[id] = response;
    command_order_.push_back(id);
    if (command_order_.size() > 128) {
      command_cache_.erase(command_order_.front());
      command_order_.pop_front();
    }
  }

  void on_command(const SafetyCommand::Request::SharedPtr request,
                  SafetyCommand::Response::SharedPtr response) {
    if (request->request_id.empty()) {
      response->accepted = false;
      response->reason_code = "SAFETY_REQUEST_ID_REQUIRED";
      response->detail = "request_id is required";
      response->state = make_state();
      return;
    }
    if (const auto it = command_cache_.find(request->request_id);
        it != command_cache_.end()) {
      *response = it->second;
      return;
    }

    using Event = safety_manager::core::Event;
    bool accepted = false;
    std::string reason_code;
    std::string detail;
    switch (request->operation) {
      case SafetyCommand::Request::ENGAGE_SOFTWARE_ESTOP:
        software_estop_active_ = true;
        accepted = dispatch(Event::EmergencyStopEngaged, request->operator_id,
                            "SAFETY_ESTOP_ENGAGED");
        break;
      case SafetyCommand::Request::RELEASE_SOFTWARE_ESTOP:
        software_estop_active_ = false;
        if (physical_estop_active_) {
          reason_code = "SAFETY_PHYSICAL_ESTOP_ACTIVE";
          detail = "physical E-stop remains active";
        } else if (fsm_.state() == safety_manager::core::State::EmergencyStopLatched) {
          accepted = dispatch(Event::EmergencyStopReleased, request->operator_id,
                              "SAFETY_ESTOP_RELEASED");
        } else {
          accepted = true;
        }
        break;
      case SafetyCommand::Request::RESUME:
        if ((require_heartbeat_ && !heartbeat_alive_) || physical_estop_active_ ||
            software_estop_active_) {
          reason_code = "SAFETY_RESUME_GUARD_FAILED";
          detail = "heartbeat and E-stop guards are not ready";
        } else {
          accepted = dispatch(Event::ResumeRequested, request->operator_id,
                              "SAFETY_RESUME_REQUESTED");
        }
        break;
      case SafetyCommand::Request::CLEAR_FAULT:
        if (physical_estop_active_ || software_estop_active_) {
          reason_code = "SAFETY_ESTOP_ENGAGED";
          detail = "release E-stop before clearing a fault";
        } else {
          accepted = dispatch(Event::ClearFault, request->operator_id,
                              "SAFETY_FAULT_CLEARED");
        }
        break;
      default:
        reason_code = "SAFETY_COMMAND_INVALID";
        detail = "unknown safety command operation";
        break;
    }

    response->accepted = accepted;
    response->state = make_state();
    response->reason_code = accepted ? last_reason_code_ : reason_code;
    response->detail = accepted ? last_detail_ : detail;
    remember(request->request_id, *response);
  }

  void on_event(const SafetyEvent::SharedPtr message) {
    using Event = safety_manager::core::Event;
    if (message->event == SafetyEvent::REQUEST_STOP) {
      dispatch(Event::StopRequested, message->source, message->reason_code, message->detail);
    } else if (message->event == SafetyEvent::HEALTH_FAULT) {
      dispatch(Event::HealthFault, message->source, message->reason_code, message->detail);
    } else {
      RCLCPP_WARN(get_logger(), "Ignored unknown safety event: %u", message->event);
    }
  }

  void on_physical_estop(const std_msgs::msg::Bool::SharedPtr message) {
    physical_estop_active_ = message->data;
    update_estop_state("physical_estop");
  }

  void update_estop_state(const std::string & source) {
    using Event = safety_manager::core::Event;
    if (physical_estop_active_ || software_estop_active_) {
      dispatch(Event::EmergencyStopEngaged, source, "SAFETY_ESTOP_ENGAGED");
    } else if (fsm_.state() == safety_manager::core::State::EmergencyStopLatched) {
      dispatch(Event::EmergencyStopReleased, source, "SAFETY_ESTOP_RELEASED");
    }
  }

  void on_heartbeat(const SafetyHeartbeat::SharedPtr message) {
    if (!message->alive) {
      if (heartbeat_alive_) {
        dispatch(safety_manager::core::Event::LinkLost, message->source,
                 "SAFETY_COMM_TIMEOUT_STOP");
      }
      heartbeat_alive_ = false;
      return;
    }

    last_heartbeat_ = std::chrono::steady_clock::now();
    heartbeat_alive_ = true;
    if (fsm_.state() == safety_manager::core::State::Initializing) {
      dispatch(safety_manager::core::Event::InputsReady, message->source,
               "SAFETY_INPUTS_READY");
    }
  }

  void tick() {
    if (require_heartbeat_ && heartbeat_alive_ &&
        std::chrono::steady_clock::now() - last_heartbeat_ > heartbeat_timeout_) {
      heartbeat_alive_ = false;
      dispatch(safety_manager::core::Event::WatchdogTimeout, "safety_manager",
               "SAFETY_WATCHDOG_TIMEOUT");
    }
    publish();
  }

  bool dispatch(safety_manager::core::Event event, const std::string & source,
                const std::string & reason_code, const std::string & detail = {}) {
    const auto transition = fsm_.dispatch(event);
    if (!transition.accepted) {
      last_detail_ = transition.reason;
      return false;
    }
    last_source_ = source;
    last_reason_code_ = reason_code;
    last_detail_ = detail.empty() ? transition.reason : detail;
    ++sequence_;
    RCLCPP_WARN(get_logger(), "Safety %s -> %s: %s",
      safety_manager::core::to_string(transition.from),
      safety_manager::core::to_string(transition.to), transition.reason);
    publish();
    return true;
  }

  void publish() { state_pub_->publish(make_state()); }

  bool require_heartbeat_{};
  bool heartbeat_alive_{false};
  bool physical_estop_active_{false};
  bool software_estop_active_{false};
  uint64_t sequence_{0};
  std::chrono::milliseconds heartbeat_timeout_{750};
  std::chrono::steady_clock::time_point last_heartbeat_{};
  std::string last_source_{"startup"};
  std::string last_reason_code_{"SAFETY_INITIALIZING"};
  std::string last_detail_{"waiting for required safety inputs"};
  safety_manager::core::StateMachine fsm_;
  std::unordered_map<std::string, SafetyCommand::Response> command_cache_;
  std::deque<std::string> command_order_;
  rclcpp::Publisher<SafetyState>::SharedPtr state_pub_;
  rclcpp::Service<SafetyCommand>::SharedPtr command_service_;
  rclcpp::Subscription<SafetyEvent>::SharedPtr event_sub_;
  rclcpp::Subscription<std_msgs::msg::Bool>::SharedPtr physical_estop_sub_;
  rclcpp::Subscription<SafetyHeartbeat>::SharedPtr heartbeat_sub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace

int main(int argc, char ** argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<SafetyManagerNode>());
  rclcpp::shutdown();
  return 0;
}
