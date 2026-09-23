// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include "mission_manager/mission_state_machine.hpp"

namespace mission_manager::core {

State StateMachine::state() const {
  return state_;
}

bool StateMachine::autonomous_motion_allowed() const {
  return state_ == State::Running || state_ == State::Returning;
}

State StateMachine::halt_target() const {
  return halt_target_;
}

State StateMachine::resume_target() const {
  return resume_target_;
}

void StateMachine::set_return_to_dock(bool enabled) {
  if (state_ == State::Idle) {
    return_to_dock_ = enabled;
  }
}

bool StateMachine::return_to_dock() const {
  return return_to_dock_;
}

Transition StateMachine::accept(State from, State to, const char * reason) {
  state_ = to;
  return {from, to, true, reason};
}

Transition StateMachine::reject(const char * reason) const {
  return {state_, state_, false, reason};
}

Transition StateMachine::begin_pausing(State from, State target, const char * reason) {
  resume_target_ = from;
  halt_target_ = target;
  return accept(from, State::Pausing, reason);
}

Transition StateMachine::dispatch(Event event) {
  const State from = state_;

  switch (state_) {
    case State::Idle:
      if (event == Event::MissionConfigured) {
        return accept(from, State::Ready, "mission configured");
      }
      if (event == Event::StopRequested) {
        return accept(from, State::Idle, "mission is already stopped");
      }
      return reject("event is invalid while idle");

    case State::Ready:
      if (event == Event::StartRequested) {
        return accept(from, State::Running, "mission started");
      }
      if (event == Event::StopRequested) {
        return accept(from, State::Idle, "configured mission cleared");
      }
      return reject("event is invalid while ready");

    case State::Running:
    case State::Returning:
      if (event == Event::PauseRequested || event == Event::ManualTakeover ||
          event == Event::LinkLost || event == Event::SafetyStop ||
          event == Event::AuthorityLost) {
        return begin_pausing(from, State::Paused,
                             "active work must halt before pausing");
      }
      if (event == Event::StopRequested) {
        return begin_pausing(from, State::Idle,
                             "active work must halt before clearing mission");
      }
      if (event == Event::FatalStepFailure) {
        return begin_pausing(from, State::Failed,
                             "active work must halt before reporting failure");
      }
      if (state_ == State::Running && event == Event::InspectionComplete) {
        if (return_to_dock_) {
          return accept(from, State::Returning, "dock approach started");
        }
        return accept(from, State::Completed, "inspection completed");
      }
      if (state_ == State::Returning && event == Event::ReturnComplete) {
        return accept(from, State::Completed, "dock approach completed");
      }
      return reject("event is invalid while mission is active");

    case State::Pausing:
      if (event == Event::MotionQuiesced) {
        return accept(from, halt_target_, "all motion is quiescent");
      }
      if (event == Event::StopRequested) {
        halt_target_ = State::Idle;
        return accept(from, State::Pausing, "stop target recorded");
      }
      if (event == Event::FatalStepFailure) {
        // An already accepted operator stop has higher priority than failure.
        if (halt_target_ != State::Idle) {
          halt_target_ = State::Failed;
        }
        return accept(from, State::Pausing, "failure recorded while halting");
      }
      if (event == Event::PauseRequested || event == Event::ManualTakeover ||
          event == Event::LinkLost || event == Event::SafetyStop ||
          event == Event::AuthorityLost) {
        return accept(from, State::Pausing, "pause is already in progress");
      }
      return reject("event is invalid while pausing");

    case State::Paused:
      if (event == Event::ResumeRequested) {
        return accept(from, State::Recovering, "recovery checks started");
      }
      if (event == Event::StopRequested) {
        return accept(from, State::Idle, "paused mission cleared");
      }
      if (event == Event::PauseRequested || event == Event::ManualTakeover ||
          event == Event::LinkLost || event == Event::SafetyStop ||
          event == Event::AuthorityLost) {
        return accept(from, State::Paused, "mission is already paused");
      }
      return reject("event is invalid while paused");

    case State::Recovering:
      if (event == Event::RecoveryReady) {
        return accept(from, resume_target_, "interrupted phase restarted");
      }
      if (event == Event::SafetyStop || event == Event::LinkLost) {
        return accept(from, State::Paused, "recovery cancelled by motion inhibit");
      }
      if (event == Event::StopRequested) {
        return accept(from, State::Idle, "recovering mission cleared");
      }
      return reject("event is invalid while recovering");

    case State::Completed:
    case State::Failed:
      if (event == Event::ResetRequested || event == Event::StopRequested) {
        return accept(from, State::Idle, "terminal mission cleared");
      }
      return reject("terminal state requires reset");
  }

  return reject("unknown mission state");
}

const char * to_string(State state) {
  switch (state) {
    case State::Idle: return "idle";
    case State::Ready: return "ready";
    case State::Running: return "running";
    case State::Pausing: return "pausing";
    case State::Paused: return "paused";
    case State::Recovering: return "recovering";
    case State::Returning: return "returning";
    case State::Completed: return "completed";
    case State::Failed: return "failed";
  }
  return "unknown";
}

const char * to_string(Event event) {
  switch (event) {
    case Event::MissionConfigured: return "mission_configured";
    case Event::StartRequested: return "start_requested";
    case Event::PauseRequested: return "pause_requested";
    case Event::ManualTakeover: return "manual_takeover";
    case Event::LinkLost: return "link_lost";
    case Event::SafetyStop: return "safety_stop";
    case Event::AuthorityLost: return "authority_lost";
    case Event::StopRequested: return "stop_requested";
    case Event::FatalStepFailure: return "fatal_step_failure";
    case Event::MotionQuiesced: return "motion_quiesced";
    case Event::ResumeRequested: return "resume_requested";
    case Event::RecoveryReady: return "recovery_ready";
    case Event::InspectionComplete: return "inspection_complete";
    case Event::ReturnComplete: return "return_complete";
    case Event::ResetRequested: return "reset_requested";
  }
  return "unknown";
}

}  // namespace mission_manager::core
