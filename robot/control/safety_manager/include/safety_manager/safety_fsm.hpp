// Copyright (c) 2026 WeGo Robotics. All rights reserved.

#pragma once

namespace safety_manager {

enum class SafetyState {
  kNormal,
  kControlledStop,
  kEmergencyStopLatched,
  kFault,
};

enum class SafetyEvent {
  kRequestStop,
  kWatchdogTimeout,
  kHealthFault,
  kEmergencyStopPressed,
  kEmergencyStopReleased,
  kClearFault,
  kResume,
};

struct Transition {
  SafetyState from;
  SafetyState to;
  bool accepted;
  const char * reason;
};

// This is operational safety supervision, not a replacement for a hardware
// E-stop circuit.  It owns the only ROS-level safety state that gates commands.
class SafetyFsm {
public:
  [[nodiscard]] SafetyState state() const;
  [[nodiscard]] bool motion_permitted() const;
  Transition dispatch(SafetyEvent event);

private:
  SafetyState state_{SafetyState::kNormal};
};

const char * to_string(SafetyState state);
const char * to_string(SafetyEvent event);

}  // namespace safety_manager
