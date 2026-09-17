// Copyright (c) 2026 WeGo Robotics. All rights reserved.

#include <cstdlib>

#include "motion_interlock_manager/motion_interlock.hpp"

int main() {
  using motion_interlock_manager::MotionAuthority;
  using motion_interlock_manager::MotionInterlock;
  using motion_interlock_manager::Request;
  MotionInterlock interlock;

  if (!interlock.request(Request::kBase).accepted || interlock.state() != MotionAuthority::kBaseActive) return 1;
  if (!interlock.request(Request::kArm).accepted || interlock.state() != MotionAuthority::kBaseStopping) return 1;
  if (!interlock.base_stopped().accepted || interlock.state() != MotionAuthority::kArmActive) return 1;
  if (!interlock.request(Request::kRelease).accepted || interlock.state() != MotionAuthority::kArmStopping) return 1;
  if (!interlock.arm_stopped().accepted || interlock.state() != MotionAuthority::kNone) return 1;
  return 0;
}
