// Copyright (c) 2026 WeGo Robotics. All rights reserved.

#include <array>
#include <cstdlib>
#include <iostream>

#include "mission_manager/mission_state_machine.hpp"

namespace {

using mission_manager::core::Event;
using mission_manager::core::State;
using mission_manager::core::StateMachine;

void expect(bool condition, const char * message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
  }
}

void configure_and_start(StateMachine & fsm, bool return_to_dock = false) {
  fsm.set_return_to_dock(return_to_dock);
  expect(fsm.dispatch(Event::MissionConfigured).accepted,
         "valid configuration must be accepted");
  expect(fsm.state() == State::Ready, "configuration must enter READY");
  expect(fsm.dispatch(Event::StartRequested).accepted, "start must be accepted");
  expect(fsm.state() == State::Running, "start must enter RUNNING");
}

void test_ready_is_required_before_start() {
  StateMachine fsm;
  const auto rejected = fsm.dispatch(Event::StartRequested);
  expect(!rejected.accepted, "start must be rejected before configuration");
  expect(fsm.state() == State::Idle, "rejected start must not change state");
  configure_and_start(fsm);
}

void test_pause_waits_for_motion_quiescence() {
  StateMachine fsm;
  configure_and_start(fsm);

  expect(fsm.dispatch(Event::PauseRequested).accepted, "pause must be accepted");
  expect(fsm.state() == State::Pausing, "pause must first enter PAUSING");
  expect(!fsm.autonomous_motion_allowed(), "PAUSING must inhibit autonomous motion");
  expect(fsm.dispatch(Event::PauseRequested).accepted,
         "a repeated pause must be idempotent");
  expect(fsm.state() == State::Pausing, "repeated pause must remain PAUSING");

  expect(fsm.dispatch(Event::MotionQuiesced).accepted,
         "motion confirmation must complete pausing");
  expect(fsm.state() == State::Paused, "quiescence must enter PAUSED");
}

void test_resume_revalidates_and_restarts_interrupted_phase() {
  StateMachine fsm;
  configure_and_start(fsm, true);
  fsm.dispatch(Event::InspectionComplete);
  expect(fsm.state() == State::Returning, "dock-enabled mission must return");

  fsm.dispatch(Event::ManualTakeover);
  expect(fsm.resume_target() == State::Returning,
         "returning phase must be retained as resume target");
  fsm.dispatch(Event::MotionQuiesced);
  fsm.dispatch(Event::ResumeRequested);
  expect(fsm.state() == State::Recovering, "resume must enter RECOVERING first");
  expect(!fsm.autonomous_motion_allowed(), "RECOVERING must inhibit motion");
  fsm.dispatch(Event::RecoveryReady);
  expect(fsm.state() == State::Returning,
         "recovery must restart the interrupted returning phase");
}

void test_stop_and_failure_share_the_quiescence_barrier() {
  StateMachine stopped;
  configure_and_start(stopped);
  stopped.dispatch(Event::StopRequested);
  expect(stopped.state() == State::Pausing, "active stop must enter PAUSING");
  expect(stopped.halt_target() == State::Idle, "stop target must be IDLE");
  stopped.dispatch(Event::MotionQuiesced);
  expect(stopped.state() == State::Idle, "stop must wait for quiescence before IDLE");

  StateMachine failed;
  configure_and_start(failed);
  failed.dispatch(Event::FatalStepFailure);
  expect(failed.state() == State::Pausing, "active failure must enter PAUSING");
  expect(failed.halt_target() == State::Failed, "failure target must be FAILED");
  failed.dispatch(Event::MotionQuiesced);
  expect(failed.state() == State::Failed,
         "failure must wait for quiescence before FAILED");
  failed.dispatch(Event::ResetRequested);
  expect(failed.state() == State::Idle, "reset must clear terminal state");
}

void test_stop_priority_is_preserved_while_pausing() {
  StateMachine fsm;
  configure_and_start(fsm);
  fsm.dispatch(Event::PauseRequested);
  fsm.dispatch(Event::StopRequested);
  fsm.dispatch(Event::FatalStepFailure);
  expect(fsm.halt_target() == State::Idle,
         "operator stop must outrank a later BT failure");
  fsm.dispatch(Event::MotionQuiesced);
  expect(fsm.state() == State::Idle, "higher-priority stop target must win");
}

void test_safety_hold_cancels_recovery() {
  StateMachine fsm;
  configure_and_start(fsm);
  fsm.dispatch(Event::LinkLost);
  fsm.dispatch(Event::MotionQuiesced);
  fsm.dispatch(Event::ResumeRequested);
  fsm.dispatch(Event::SafetyStop);
  expect(fsm.state() == State::Paused, "safety hold must cancel recovery");
}

void test_authority_loss_halts_active_motion_and_cancels_recovery() {
  StateMachine fsm;
  configure_and_start(fsm);
  fsm.dispatch(Event::AuthorityLost);
  expect(fsm.state() == State::Pausing, "authority loss must begin pausing");
  fsm.dispatch(Event::MotionQuiesced);
  expect(fsm.state() == State::Paused, "authority loss must settle in PAUSED");

  fsm.dispatch(Event::ResumeRequested);
  expect(fsm.state() == State::Recovering, "resume must begin recovery checks");
  fsm.dispatch(Event::AuthorityLost);
  expect(fsm.state() == State::Paused, "authority loss must cancel recovery");
}

void test_completion_respects_return_policy() {
  StateMachine direct;
  configure_and_start(direct, false);
  direct.dispatch(Event::InspectionComplete);
  expect(direct.state() == State::Completed,
         "mission without dock approach must complete directly");

  StateMachine returning;
  configure_and_start(returning, true);
  returning.dispatch(Event::InspectionComplete);
  expect(returning.state() == State::Returning,
         "mission with dock approach must enter RETURNING");
  returning.dispatch(Event::ReturnComplete);
  expect(returning.state() == State::Completed,
         "return completion must enter COMPLETED");
}

void test_invalid_events_do_not_change_state() {
  StateMachine fsm;
  fsm.dispatch(Event::MissionConfigured);
  const auto transition = fsm.dispatch(Event::RecoveryReady);
  expect(!transition.accepted, "invalid READY event must be rejected");
  expect(transition.from == State::Ready && transition.to == State::Ready,
         "rejected transition must preserve its state");
}

StateMachine make_state(State target) {
  StateMachine fsm;
  if (target == State::Idle) return fsm;

  fsm.set_return_to_dock(target == State::Returning);
  fsm.dispatch(Event::MissionConfigured);
  if (target == State::Ready) return fsm;

  fsm.dispatch(Event::StartRequested);
  if (target == State::Running) return fsm;
  if (target == State::Returning) {
    fsm.dispatch(Event::InspectionComplete);
    return fsm;
  }
  if (target == State::Completed) {
    fsm.dispatch(Event::InspectionComplete);
    return fsm;
  }
  if (target == State::Failed) {
    fsm.dispatch(Event::FatalStepFailure);
    fsm.dispatch(Event::MotionQuiesced);
    return fsm;
  }

  fsm.dispatch(Event::PauseRequested);
  if (target == State::Pausing) return fsm;
  fsm.dispatch(Event::MotionQuiesced);
  if (target == State::Paused) return fsm;
  fsm.dispatch(Event::ResumeRequested);
  return fsm;
}

bool expected_acceptance(State state, Event event) {
  switch (state) {
    case State::Idle:
      return event == Event::MissionConfigured || event == Event::StopRequested;
    case State::Ready:
      return event == Event::StartRequested || event == Event::StopRequested;
    case State::Running:
      return event == Event::PauseRequested || event == Event::ManualTakeover ||
             event == Event::LinkLost || event == Event::SafetyStop ||
             event == Event::AuthorityLost ||
             event == Event::StopRequested || event == Event::FatalStepFailure ||
             event == Event::InspectionComplete;
    case State::Pausing:
      return event == Event::PauseRequested || event == Event::ManualTakeover ||
             event == Event::LinkLost || event == Event::SafetyStop ||
             event == Event::AuthorityLost ||
             event == Event::StopRequested || event == Event::FatalStepFailure ||
             event == Event::MotionQuiesced;
    case State::Paused:
      return event == Event::PauseRequested || event == Event::ManualTakeover ||
             event == Event::LinkLost || event == Event::SafetyStop ||
             event == Event::AuthorityLost ||
             event == Event::StopRequested || event == Event::ResumeRequested;
    case State::Recovering:
      return event == Event::LinkLost || event == Event::SafetyStop ||
             event == Event::AuthorityLost ||
             event == Event::StopRequested || event == Event::RecoveryReady;
    case State::Returning:
      return event == Event::PauseRequested || event == Event::ManualTakeover ||
             event == Event::LinkLost || event == Event::SafetyStop ||
             event == Event::AuthorityLost ||
             event == Event::StopRequested || event == Event::FatalStepFailure ||
             event == Event::ReturnComplete;
    case State::Completed:
    case State::Failed:
      return event == Event::StopRequested || event == Event::ResetRequested;
  }
  return false;
}

void test_complete_state_event_acceptance_matrix() {
  constexpr std::array states{
    State::Idle, State::Ready, State::Running, State::Pausing, State::Paused,
    State::Recovering, State::Returning, State::Completed, State::Failed};
  constexpr std::array events{
    Event::MissionConfigured, Event::StartRequested, Event::PauseRequested,
    Event::ManualTakeover, Event::LinkLost, Event::SafetyStop, Event::AuthorityLost,
    Event::StopRequested, Event::FatalStepFailure, Event::MotionQuiesced,
    Event::ResumeRequested, Event::RecoveryReady, Event::InspectionComplete,
    Event::ReturnComplete, Event::ResetRequested};

  for (const auto state : states) {
    for (const auto event : events) {
      auto fsm = make_state(state);
      expect(fsm.state() == state, "test fixture must reach requested state");
      const auto transition = fsm.dispatch(event);
      expect(transition.accepted == expected_acceptance(state, event),
             "state-event acceptance matrix mismatch");
      if (!transition.accepted) {
        expect(transition.to == state && fsm.state() == state,
               "rejected matrix event must preserve state");
      }
    }
  }
}

}  // namespace

int main() {
  test_ready_is_required_before_start();
  test_pause_waits_for_motion_quiescence();
  test_resume_revalidates_and_restarts_interrupted_phase();
  test_stop_and_failure_share_the_quiescence_barrier();
  test_stop_priority_is_preserved_while_pausing();
  test_safety_hold_cancels_recovery();
  test_authority_loss_halts_active_motion_and_cancels_recovery();
  test_completion_respects_return_policy();
  test_invalid_events_do_not_change_state();
  test_complete_state_event_acceptance_matrix();
  return 0;
}
