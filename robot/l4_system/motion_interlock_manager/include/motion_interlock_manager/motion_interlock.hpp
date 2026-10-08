// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#pragma once

namespace motion_interlock_manager {

enum class MotionAuthority {
  None,
  BaseActive,
  BaseStopping,
  ArmActive,
  ArmStopping,
};

enum class Request { Base, Arm, Release };

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
  [[nodiscard]] MotionAuthority pending() const;
  Transition request(Request request);
  Transition base_stopped();
  Transition arm_stopped();
  Transition transition_timeout();

private:
  MotionAuthority state_{MotionAuthority::None};
  MotionAuthority pending_{MotionAuthority::None};
};

const char * to_string(MotionAuthority state);

}  // namespace motion_interlock_manager
