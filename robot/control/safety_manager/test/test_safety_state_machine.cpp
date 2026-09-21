// Copyright (c) 2026 WeGo Robotics. All rights reserved.

#include <array>
#include <cstdlib>
#include <iostream>

#include "safety_manager/safety_state_machine.hpp"

namespace {

using safety_manager::core::Event;
using safety_manager::core::State;
using safety_manager::core::StateMachine;

void expect(bool condition, const char * message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
  }
}

StateMachine make_state(State target) {
  StateMachine fsm;
  if (target == State::Initializing) return fsm;
  if (target == State::EmergencyStopLatched) {
    fsm.dispatch(Event::EmergencyStopEngaged);
    return fsm;
  }
  if (target == State::Fault) {
    fsm.dispatch(Event::HealthFault);
    return fsm;
  }
  fsm.dispatch(Event::InputsReady);
  if (target == State::ControlledStop) return fsm;
  fsm.dispatch(Event::ResumeRequested);
  return fsm;
}

bool expected_acceptance(State state, Event event) {
  if (event == Event::EmergencyStopEngaged || event == Event::HealthFault) return true;
  switch (state) {
    case State::Initializing:
      return event == Event::InputsReady;
    case State::ControlledStop:
      return event == Event::StopRequested || event == Event::LinkLost ||
             event == Event::WatchdogTimeout || event == Event::ResumeRequested;
    case State::Normal:
      return event == Event::StopRequested || event == Event::LinkLost ||
             event == Event::WatchdogTimeout;
    case State::EmergencyStopLatched:
      return event == Event::EmergencyStopReleased;
    case State::Fault:
      return event == Event::ClearFault;
  }
  return false;
}

void test_fail_closed_startup_and_explicit_resume() {
  StateMachine fsm;
  expect(fsm.state() == State::Initializing, "startup must be INITIALIZING");
  expect(!fsm.motion_permitted(), "startup must inhibit motion");
  fsm.dispatch(Event::InputsReady);
  expect(fsm.state() == State::ControlledStop,
         "fresh inputs must not automatically permit motion");
  fsm.dispatch(Event::ResumeRequested);
  expect(fsm.state() == State::Normal && fsm.motion_permitted(),
         "only explicit resume may enter NORMAL");
}

void test_estop_release_never_resumes_motion() {
  auto fsm = make_state(State::Normal);
  fsm.dispatch(Event::EmergencyStopEngaged);
  fsm.dispatch(Event::EmergencyStopReleased);
  expect(fsm.state() == State::ControlledStop,
         "E-stop release must require a separate resume");
  expect(!fsm.motion_permitted(), "E-stop release must keep motion inhibited");
}

void test_fault_survives_estop_latch() {
  StateMachine fsm;
  fsm.dispatch(Event::HealthFault);
  fsm.dispatch(Event::EmergencyStopEngaged);
  fsm.dispatch(Event::EmergencyStopReleased);
  expect(fsm.state() == State::Fault, "E-stop release must restore latched fault");
  fsm.dispatch(Event::ClearFault);
  expect(fsm.state() == State::ControlledStop,
         "fault clear must still require operator resume");
}

void test_fault_during_estop_is_retained() {
  StateMachine fsm;
  fsm.dispatch(Event::EmergencyStopEngaged);
  fsm.dispatch(Event::HealthFault);
  expect(fsm.fault_latched(), "fault during E-stop must be latched");
  fsm.dispatch(Event::EmergencyStopReleased);
  expect(fsm.state() == State::Fault, "latched fault must be visible after release");
}

void test_complete_state_event_acceptance_matrix() {
  constexpr std::array states{State::Initializing, State::ControlledStop, State::Normal,
                              State::EmergencyStopLatched, State::Fault};
  constexpr std::array events{
    Event::InputsReady, Event::StopRequested, Event::LinkLost,
    Event::WatchdogTimeout, Event::HealthFault, Event::EmergencyStopEngaged,
    Event::EmergencyStopReleased, Event::ClearFault, Event::ResumeRequested};

  for (const auto state : states) {
    for (const auto event : events) {
      auto fsm = make_state(state);
      const auto transition = fsm.dispatch(event);
      expect(transition.accepted == expected_acceptance(state, event),
             "safety state-event acceptance matrix mismatch");
      if (!transition.accepted) {
        expect(fsm.state() == state, "rejected safety event must preserve state");
      }
    }
  }
}

}  // namespace

int main() {
  test_fail_closed_startup_and_explicit_resume();
  test_estop_release_never_resumes_motion();
  test_fault_survives_estop_latch();
  test_fault_during_estop_is_retained();
  test_complete_state_event_acceptance_matrix();
  return 0;
}
