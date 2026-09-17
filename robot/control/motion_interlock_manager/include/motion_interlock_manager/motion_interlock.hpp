// Copyright (c) 2026 WeGo Robotics. All rights reserved.

#pragma once

namespace motion_interlock_manager {

enum class MotionAuthority {
  kNone,
  kBaseActive,
  kBaseStopping,
  kArmActive,
  kArmStopping,
};

enum class Request { kBase, kArm, kRelease };

struct Transition {
  MotionAuthority from;
  MotionAuthority to;
  bool accepted;
  const char * reason;
};

// Owns operational exclusivity only.  It does not override SafetyManager or a
// physical E-stop; SafetyGate requires both this authority and safety permit.
class MotionInterlock {
public:
  [[nodiscard]] MotionAuthority state() const;
  Transition request(Request request);
  Transition base_stopped();
  Transition arm_stopped();

private:
  MotionAuthority state_{MotionAuthority::kNone};
  MotionAuthority pending_{MotionAuthority::kNone};
};

const char * to_string(MotionAuthority state);

}  // namespace motion_interlock_manager
