// Copyright (c) 2026 WeGo Robotics. All rights reserved.

#include "safety_manager/safety_fsm.hpp"

namespace safety_manager {
namespace {

Transition accept(SafetyState from, SafetyState to, const char * reason) {
  return {from, to, true, reason};
}

Transition reject(SafetyState state, const char * reason) {
  return {state, state, false, reason};
}

}  // namespace

SafetyState SafetyFsm::state() const { return state_; }

bool SafetyFsm::motion_permitted() const { return state_ == SafetyState::kNormal; }

Transition SafetyFsm::dispatch(SafetyEvent event) {
  const SafetyState from = state_;

  // A physical E-stop indication always wins, from every state. Releasing it
  // never restores motion; a separate, explicit resume is still required.
  if (event == SafetyEvent::kEmergencyStopPressed) {
    state_ = SafetyState::kEmergencyStopLatched;
    return accept(from, state_, "physical emergency stop latched");
  }
  if (state_ == SafetyState::kEmergencyStopLatched) {
    if (event == SafetyEvent::kEmergencyStopReleased) {
      state_ = SafetyState::kControlledStop;
      return accept(from, state_, "emergency stop released; operator resume required");
    }
    return reject(state_, "emergency stop remains latched");
  }

  if (event == SafetyEvent::kHealthFault) {
    state_ = SafetyState::kFault;
    return accept(from, state_, "health or process fault latched");
  }

  switch (state_) {
    case SafetyState::kNormal:
      if (event == SafetyEvent::kRequestStop || event == SafetyEvent::kWatchdogTimeout) {
        state_ = SafetyState::kControlledStop;
        return accept(from, state_, "controlled stop requested");
      }
      return reject(state_, "event is invalid while normal");

    case SafetyState::kControlledStop:
      if (event == SafetyEvent::kResume) {
        state_ = SafetyState::kNormal;
        return accept(from, state_, "operator explicitly resumed motion");
      }
      return reject(state_, "controlled stop requires an explicit resume");

    case SafetyState::kFault:
      if (event == SafetyEvent::kClearFault) {
        state_ = SafetyState::kControlledStop;
        return accept(from, state_, "fault cleared; operator resume required");
      }
      return reject(state_, "fault requires an explicit clear");

    case SafetyState::kEmergencyStopLatched:
      break;
  }
  return reject(state_, "unknown safety state");
}

const char * to_string(SafetyState state) {
  switch (state) {
    case SafetyState::kNormal: return "normal";
    case SafetyState::kControlledStop: return "controlled_stop";
    case SafetyState::kEmergencyStopLatched: return "e_stop_latched";
    case SafetyState::kFault: return "fault";
  }
  return "unknown";
}

const char * to_string(SafetyEvent event) {
  switch (event) {
    case SafetyEvent::kRequestStop: return "request_stop";
    case SafetyEvent::kWatchdogTimeout: return "watchdog_timeout";
    case SafetyEvent::kHealthFault: return "health_fault";
    case SafetyEvent::kEmergencyStopPressed: return "emergency_stop_pressed";
    case SafetyEvent::kEmergencyStopReleased: return "emergency_stop_released";
    case SafetyEvent::kClearFault: return "clear_fault";
    case SafetyEvent::kResume: return "resume";
  }
  return "unknown";
}

}  // namespace safety_manager
