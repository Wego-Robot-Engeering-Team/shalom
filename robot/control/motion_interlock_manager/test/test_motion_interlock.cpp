// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include <array>
#include <cstdlib>
#include <iostream>

#include "motion_interlock_manager/motion_interlock.hpp"

namespace {

using motion_interlock_manager::MotionAuthority;
using motion_interlock_manager::MotionInterlock;
using motion_interlock_manager::Request;

void expect(bool condition, const char * message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
  }
}

void test_authority_switch_waits_for_stopped_feedback() {
  MotionInterlock interlock;

  expect(interlock.request(Request::Base).accepted, "base authority must be granted");
  expect(interlock.state() == MotionAuthority::BaseActive, "base must become active");
  expect(interlock.request(Request::Arm).accepted, "arm switch must begin");
  expect(interlock.state() == MotionAuthority::BaseStopping,
         "arm cannot become active before base stops");
  expect(interlock.pending() == MotionAuthority::ArmActive,
         "arm authority must be pending");
  expect(!interlock.arm_stopped().accepted, "wrong stopped feedback must be rejected");
  expect(interlock.base_stopped().accepted, "fresh base stopped feedback must complete switch");
  expect(interlock.state() == MotionAuthority::ArmActive, "arm must become active");
}

void test_same_authority_request_is_idempotent() {
  MotionInterlock interlock;
  interlock.request(Request::Base);
  const auto repeated = interlock.request(Request::Base);
  expect(repeated.accepted && repeated.from == repeated.to,
         "same authority request must be idempotent");
}

void test_requests_are_rejected_while_stopping() {
  MotionInterlock interlock;
  interlock.request(Request::Base);
  interlock.request(Request::Arm);
  expect(!interlock.request(Request::Base).accepted,
         "new base request must be rejected while base is stopping");
  expect(!interlock.request(Request::Arm).accepted,
         "new arm request must be rejected while base is stopping");
  expect(!interlock.request(Request::Release).accepted,
         "release must be rejected while a transition is pending");
}

void test_release_waits_for_stopped_feedback() {
  MotionInterlock interlock;
  interlock.request(Request::Arm);
  interlock.request(Request::Release);
  expect(interlock.state() == MotionAuthority::ArmStopping,
         "release must wait in ARM_STOPPING");
  expect(interlock.pending() == MotionAuthority::None, "release target must be NONE");
  interlock.arm_stopped();
  expect(interlock.state() == MotionAuthority::None, "stopped feedback must finish release");
}

void test_transition_timeout_fails_closed() {
  MotionInterlock interlock;
  expect(!interlock.transition_timeout().accepted,
         "timeout outside a transition must be rejected");
  interlock.request(Request::Base);
  interlock.request(Request::Arm);
  expect(interlock.transition_timeout().accepted, "active transition timeout must be accepted");
  expect(interlock.state() == MotionAuthority::None,
         "transition timeout must clear all authority");
  expect(interlock.pending() == MotionAuthority::None,
         "transition timeout must clear pending authority");
}

void test_base_and_arm_are_never_active_together() {
  MotionInterlock interlock;
  interlock.request(Request::Base);
  interlock.request(Request::Arm);
  expect(interlock.state() == MotionAuthority::BaseStopping,
         "base-to-arm transition must not expose two active authorities");
  interlock.base_stopped();
  expect(interlock.state() == MotionAuthority::ArmActive,
         "only arm may be active after base stop confirmation");
}

enum class Input {
  RequestBase,
  RequestArm,
  Release,
  BaseStopped,
  ArmStopped,
  TransitionTimeout,
};

MotionInterlock make_state(MotionAuthority target) {
  MotionInterlock interlock;
  if (target == MotionAuthority::None) return interlock;
  if (target == MotionAuthority::BaseActive) {
    interlock.request(Request::Base);
    return interlock;
  }
  if (target == MotionAuthority::BaseStopping) {
    interlock.request(Request::Base);
    interlock.request(Request::Arm);
    return interlock;
  }
  if (target == MotionAuthority::ArmActive) {
    interlock.request(Request::Arm);
    return interlock;
  }
  interlock.request(Request::Arm);
  interlock.request(Request::Base);
  return interlock;
}

motion_interlock_manager::Transition apply(MotionInterlock & interlock, Input input) {
  switch (input) {
    case Input::RequestBase: return interlock.request(Request::Base);
    case Input::RequestArm: return interlock.request(Request::Arm);
    case Input::Release: return interlock.request(Request::Release);
    case Input::BaseStopped: return interlock.base_stopped();
    case Input::ArmStopped: return interlock.arm_stopped();
    case Input::TransitionTimeout: return interlock.transition_timeout();
  }
  std::abort();
}

bool expected_acceptance(MotionAuthority state, Input input) {
  switch (state) {
    case MotionAuthority::None:
      return input == Input::RequestBase || input == Input::RequestArm;
    case MotionAuthority::BaseActive:
    case MotionAuthority::ArmActive:
      return input == Input::RequestBase || input == Input::RequestArm ||
             input == Input::Release;
    case MotionAuthority::BaseStopping:
      return input == Input::BaseStopped || input == Input::TransitionTimeout;
    case MotionAuthority::ArmStopping:
      return input == Input::ArmStopped || input == Input::TransitionTimeout;
  }
  return false;
}

void test_complete_state_input_acceptance_matrix() {
  constexpr std::array states{
    MotionAuthority::None, MotionAuthority::BaseActive,
    MotionAuthority::BaseStopping, MotionAuthority::ArmActive,
    MotionAuthority::ArmStopping};
  constexpr std::array inputs{
    Input::RequestBase, Input::RequestArm, Input::Release,
    Input::BaseStopped, Input::ArmStopped, Input::TransitionTimeout};

  for (const auto state : states) {
    for (const auto input : inputs) {
      auto interlock = make_state(state);
      const auto transition = apply(interlock, input);
      expect(transition.accepted == expected_acceptance(state, input),
             "interlock state-input acceptance matrix mismatch");
      if (!transition.accepted) {
        expect(interlock.state() == state, "rejected interlock input must preserve state");
      }
    }
  }
}

}  // namespace

int main() {
  test_authority_switch_waits_for_stopped_feedback();
  test_same_authority_request_is_idempotent();
  test_requests_are_rejected_while_stopping();
  test_release_waits_for_stopped_feedback();
  test_transition_timeout_fails_closed();
  test_base_and_arm_are_never_active_together();
  test_complete_state_input_acceptance_matrix();
  return 0;
}
