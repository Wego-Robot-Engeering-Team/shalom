// Copyright (c) 2026 WeGo Robotics. All rights reserved.

#include "mission_manager/mission_fsm.hpp"

namespace mission_manager {
namespace {

Transition accept(MissionState from, MissionState to, const char * reason) {
  return {from, to, true, reason};
}

Transition reject(MissionState state, const char * reason) {
  return {state, state, false, reason};
}

}  // namespace

MissionState MissionFsm::state() const {
  return state_;
}

bool MissionFsm::autonomous_motion_allowed() const {
  return state_ == MissionState::kRunning || state_ == MissionState::kReturning;
}

Transition MissionFsm::dispatch(MissionEvent event) {
  const MissionState from = state_;

  // E-stop always wins.  Releasing it deliberately does not restart motion.
  if (event == MissionEvent::kEmergencyStop) {
    state_ = MissionState::kEmergencyStopped;
    return accept(from, state_, "emergency stop latched");
  }
  if (state_ == MissionState::kEmergencyStopped) {
    if (event == MissionEvent::kEmergencyStopReleased) {
      state_ = MissionState::kPaused;
      return accept(from, state_, "operator resume is still required");
    }
    return reject(state_, "emergency stop is latched");
  }

  switch (state_) {
    case MissionState::kIdle:
      if (event == MissionEvent::kStart) {
        state_ = MissionState::kRunning;
        return accept(from, state_, "mission started");
      }
      if (event == MissionEvent::kPause || event == MissionEvent::kManualTakeover ||
          event == MissionEvent::kLinkLost) {
        state_ = MissionState::kPaused;
        return accept(from, state_, "autonomy held pending explicit resume");
      }
      if (event == MissionEvent::kBatteryLow) {
        state_ = MissionState::kReturning;
        return accept(from, state_, "low battery return requested");
      }
      return reject(state_, "event is invalid while idle");

    case MissionState::kRunning:
      if (event == MissionEvent::kPause || event == MissionEvent::kManualTakeover ||
          event == MissionEvent::kLinkLost) {
        state_ = MissionState::kPaused;
        return accept(from, state_, "autonomy paused and active work must be cancelled");
      }
      if (event == MissionEvent::kBatteryLow || event == MissionEvent::kMissionComplete) {
        state_ = MissionState::kReturning;
        return accept(from, state_, "return-to-dock phase started");
      }
      if (event == MissionEvent::kStepFailed) {
        state_ = MissionState::kFault;
        return accept(from, state_, "inspection step failed");
      }
      if (event == MissionEvent::kStop) {
        state_ = MissionState::kIdle;
        return accept(from, state_, "mission stopped by operator");
      }
      return reject(state_, "event is invalid while running");

    case MissionState::kPaused:
      if (event == MissionEvent::kResume || event == MissionEvent::kStart) {
        state_ = MissionState::kRunning;
        return accept(from, state_, "operator explicitly resumed autonomy");
      }
      if (event == MissionEvent::kBatteryLow) {
        state_ = MissionState::kReturning;
        return accept(from, state_, "low battery return requested");
      }
      if (event == MissionEvent::kStop) {
        state_ = MissionState::kIdle;
        return accept(from, state_, "paused mission cleared");
      }
      return reject(state_, "event is invalid while paused");

    case MissionState::kReturning:
      if (event == MissionEvent::kReturnComplete) {
        state_ = MissionState::kCompleted;
        return accept(from, state_, "robot reached dock");
      }
      if (event == MissionEvent::kPause || event == MissionEvent::kManualTakeover ||
          event == MissionEvent::kLinkLost) {
        state_ = MissionState::kPaused;
        return accept(from, state_, "return paused and active work must be cancelled");
      }
      if (event == MissionEvent::kStepFailed) {
        state_ = MissionState::kFault;
        return accept(from, state_, "return-to-dock failed");
      }
      if (event == MissionEvent::kStop) {
        state_ = MissionState::kIdle;
        return accept(from, state_, "return cancelled by operator");
      }
      return reject(state_, "event is invalid while returning");

    case MissionState::kCompleted:
      if (event == MissionEvent::kStart) {
        state_ = MissionState::kRunning;
        return accept(from, state_, "new mission started");
      }
      if (event == MissionEvent::kStop) {
        state_ = MissionState::kIdle;
        return accept(from, state_, "completed mission cleared");
      }
      return reject(state_, "event is invalid after completion");

    case MissionState::kFault:
      if (event == MissionEvent::kResetFault) {
        state_ = MissionState::kIdle;
        return accept(from, state_, "fault reset by operator");
      }
      return reject(state_, "fault requires an explicit reset");

    case MissionState::kEmergencyStopped:
      break;
  }
  return reject(state_, "unknown state");
}

const char * to_string(MissionState state) {
  switch (state) {
    case MissionState::kIdle: return "idle";
    case MissionState::kRunning: return "running";
    case MissionState::kPaused: return "paused";
    case MissionState::kReturning: return "returning";
    case MissionState::kCompleted: return "completed";
    case MissionState::kFault: return "fault";
    case MissionState::kEmergencyStopped: return "emergency_stopped";
  }
  return "unknown";
}

const char * to_string(MissionEvent event) {
  switch (event) {
    case MissionEvent::kStart: return "start";
    case MissionEvent::kPause: return "pause";
    case MissionEvent::kResume: return "resume";
    case MissionEvent::kManualTakeover: return "manual_takeover";
    case MissionEvent::kLinkLost: return "link_lost";
    case MissionEvent::kEmergencyStop: return "emergency_stop";
    case MissionEvent::kEmergencyStopReleased: return "emergency_stop_released";
    case MissionEvent::kBatteryLow: return "battery_low";
    case MissionEvent::kMissionComplete: return "mission_complete";
    case MissionEvent::kReturnComplete: return "return_complete";
    case MissionEvent::kStepFailed: return "step_failed";
    case MissionEvent::kStop: return "stop";
    case MissionEvent::kResetFault: return "reset_fault";
  }
  return "unknown";
}

}  // namespace mission_manager
