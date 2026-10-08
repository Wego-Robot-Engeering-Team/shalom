// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include "motion_interlock_manager/motion_interlock.hpp"

namespace motion_interlock_manager {
namespace {

Transition accept(MotionAuthority from, MotionAuthority to, const char * reason) {
  return {from, to, true, reason};
}

Transition reject(MotionAuthority state, const char * reason) {
  return {state, state, false, reason};
}

}  // namespace

MotionAuthority MotionInterlock::state() const { return state_; }

MotionAuthority MotionInterlock::pending() const { return pending_; }

Transition MotionInterlock::request(Request request) {
  const auto from = state_;
  const auto requested = request == Request::Base ? MotionAuthority::BaseActive :
    request == Request::Arm ? MotionAuthority::ArmActive : MotionAuthority::None;

  if (request == Request::Release) {
    if (state_ == MotionAuthority::BaseActive) {
      pending_ = MotionAuthority::None;
      state_ = MotionAuthority::BaseStopping;
      return accept(from, state_, "base release waits for stopped feedback");
    }
    if (state_ == MotionAuthority::ArmActive) {
      pending_ = MotionAuthority::None;
      state_ = MotionAuthority::ArmStopping;
      return accept(from, state_, "arm release waits for stopped feedback");
    }
    return reject(state_, "nothing active to release");
  }

  if (state_ == MotionAuthority::None) {
    state_ = requested;
    return accept(from, state_, "motion authority granted");
  }
  if (state_ == requested) return accept(from, state_, "requested authority already held");

  if (state_ == MotionAuthority::BaseActive) {
    pending_ = requested;
    state_ = MotionAuthority::BaseStopping;
    return accept(from, state_, "base must stop before arm authority is granted");
  }
  if (state_ == MotionAuthority::ArmActive) {
    pending_ = requested;
    state_ = MotionAuthority::ArmStopping;
    return accept(from, state_, "arm must stop before base authority is granted");
  }
  return reject(state_, "authority transition already waits for stopped feedback");
}

Transition MotionInterlock::base_stopped() {
  const auto from = state_;
  if (state_ != MotionAuthority::BaseStopping) return reject(state_, "base stop feedback is not awaited");
  state_ = pending_;
  pending_ = MotionAuthority::None;
  return accept(from, state_, "base stopped; pending authority applied");
}

Transition MotionInterlock::arm_stopped() {
  const auto from = state_;
  if (state_ != MotionAuthority::ArmStopping) return reject(state_, "arm stop feedback is not awaited");
  state_ = pending_;
  pending_ = MotionAuthority::None;
  return accept(from, state_, "arm stopped; pending authority applied");
}

Transition MotionInterlock::transition_timeout() {
  const auto from = state_;
  if (state_ != MotionAuthority::BaseStopping &&
      state_ != MotionAuthority::ArmStopping) {
    return reject(state_, "no authority transition is in progress");
  }
  state_ = MotionAuthority::None;
  pending_ = MotionAuthority::None;
  return accept(from, state_, "transition timed out; safety fault required");
}

const char * to_string(MotionAuthority state) {
  switch (state) {
    case MotionAuthority::None: return "none";
    case MotionAuthority::BaseActive: return "base_active";
    case MotionAuthority::BaseStopping: return "base_stopping";
    case MotionAuthority::ArmActive: return "arm_active";
    case MotionAuthority::ArmStopping: return "arm_stopping";
  }
  return "unknown";
}

}  // namespace motion_interlock_manager
