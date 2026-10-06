// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include "safety_manager/safety_state_machine.hpp"

namespace safety_manager::core {

State StateMachine::state() const { return state_; }

bool StateMachine::motion_permitted() const { return state_ == State::Normal; }

bool StateMachine::fault_latched() const { return fault_latched_; }

Transition StateMachine::accept(State from, State to, const char * reason) {
  state_ = to;
  return {from, to, true, reason};
}

Transition StateMachine::reject(const char * reason) const {
  return {state_, state_, false, reason};
}

Transition StateMachine::dispatch(Event event) {
  const State from = state_;

  // E-stop has the highest priority in every state. A pre-existing fault is
  // retained so release can return to FAULT instead of losing its cause.
  if (event == Event::EmergencyStopEngaged) {
    if (state_ == State::Fault) fault_latched_ = true;
    return accept(from, State::EmergencyStopLatched, "emergency stop latched");
  }

  if (state_ == State::EmergencyStopLatched) {
    if (event == Event::HealthFault) {
      fault_latched_ = true;
      return accept(from, State::EmergencyStopLatched,
                    "fault retained while emergency stop is active");
    }
    if (event == Event::EmergencyStopReleased) {
      return accept(from, fault_latched_ ? State::Fault : State::ControlledStop,
                    fault_latched_ ? "emergency stop released with fault latched"
                                   : "emergency stop released; resume required");
    }
    return reject("emergency stop remains latched");
  }

  if (event == Event::HealthFault) {
    fault_latched_ = true;
    return accept(from, State::Fault, "health fault latched");
  }

  switch (state_) {
    case State::Initializing:
      if (event == Event::InputsReady) {
        return accept(from, State::ControlledStop,
                      "inputs are ready; operator resume required");
      }
      return reject("required safety inputs are not ready");

    case State::ControlledStop:
      if (event == Event::ResumeRequested) {
        return accept(from, State::Normal, "operator explicitly resumed motion");
      }
      if (event == Event::StopRequested || event == Event::LinkLost ||
          event == Event::WatchdogTimeout) {
        return accept(from, State::ControlledStop, "motion is already inhibited");
      }
      return reject("controlled stop requires an explicit resume");

    case State::Normal:
      if (event == Event::StopRequested || event == Event::LinkLost ||
          event == Event::WatchdogTimeout) {
        return accept(from, State::ControlledStop, "controlled stop requested");
      }
      return reject("event is invalid while normal");

    case State::Fault:
      if (event == Event::ClearFault) {
        fault_latched_ = false;
        return accept(from, State::ControlledStop,
                      "fault cleared; operator resume required");
      }
      return reject("fault requires an explicit clear");

    case State::EmergencyStopLatched:
      break;
  }
  return reject("unknown safety state");
}

const char * to_string(State state) {
  switch (state) {
    case State::Initializing: return "initializing";
    case State::ControlledStop: return "controlled_stop";
    case State::Normal: return "normal";
    case State::EmergencyStopLatched: return "e_stop_latched";
    case State::Fault: return "fault";
  }
  return "unknown";
}

const char * to_string(Event event) {
  switch (event) {
    case Event::InputsReady: return "inputs_ready";
    case Event::StopRequested: return "stop_requested";
    case Event::LinkLost: return "link_lost";
    case Event::WatchdogTimeout: return "watchdog_timeout";
    case Event::HealthFault: return "health_fault";
    case Event::EmergencyStopEngaged: return "estop_engaged";
    case Event::EmergencyStopReleased: return "estop_released";
    case Event::ClearFault: return "clear_fault";
    case Event::ResumeRequested: return "resume_requested";
  }
  return "unknown";
}

}  // namespace safety_manager::core
