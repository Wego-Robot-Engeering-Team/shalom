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

Transition MotionInterlock::request(Request request) {
  const auto from = state_;
  const auto requested = request == Request::kBase ? MotionAuthority::kBaseActive :
    request == Request::kArm ? MotionAuthority::kArmActive : MotionAuthority::kNone;

  if (request == Request::kRelease) {
    if (state_ == MotionAuthority::kBaseActive) {
      pending_ = MotionAuthority::kNone;
      state_ = MotionAuthority::kBaseStopping;
      return accept(from, state_, "base release waits for stopped feedback");
    }
    if (state_ == MotionAuthority::kArmActive) {
      pending_ = MotionAuthority::kNone;
      state_ = MotionAuthority::kArmStopping;
      return accept(from, state_, "arm release waits for stopped feedback");
    }
    return reject(state_, "nothing active to release");
  }

  if (state_ == MotionAuthority::kNone) {
    state_ = requested;
    return accept(from, state_, "motion authority granted");
  }
  if (state_ == requested) return accept(from, state_, "requested authority already held");

  if (state_ == MotionAuthority::kBaseActive) {
    pending_ = requested;
    state_ = MotionAuthority::kBaseStopping;
    return accept(from, state_, "base must stop before arm authority is granted");
  }
  if (state_ == MotionAuthority::kArmActive) {
    pending_ = requested;
    state_ = MotionAuthority::kArmStopping;
    return accept(from, state_, "arm must stop before base authority is granted");
  }
  return reject(state_, "authority transition already waits for stopped feedback");
}

Transition MotionInterlock::base_stopped() {
  const auto from = state_;
  if (state_ != MotionAuthority::kBaseStopping) return reject(state_, "base stop feedback is not awaited");
  state_ = pending_;
  pending_ = MotionAuthority::kNone;
  return accept(from, state_, "base stopped; pending authority applied");
}

Transition MotionInterlock::arm_stopped() {
  const auto from = state_;
  if (state_ != MotionAuthority::kArmStopping) return reject(state_, "arm stop feedback is not awaited");
  state_ = pending_;
  pending_ = MotionAuthority::kNone;
  return accept(from, state_, "arm stopped; pending authority applied");
}

const char * to_string(MotionAuthority state) {
  switch (state) {
    case MotionAuthority::kNone: return "none";
    case MotionAuthority::kBaseActive: return "base_active";
    case MotionAuthority::kBaseStopping: return "base_stopping";
    case MotionAuthority::kArmActive: return "arm_active";
    case MotionAuthority::kArmStopping: return "arm_stopping";
  }
  return "unknown";
}

}  // namespace motion_interlock_manager
