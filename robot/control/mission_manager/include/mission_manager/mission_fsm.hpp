// Copyright (c) 2026 WeGo Robotics. All rights reserved.

#pragma once

namespace mission_manager {

enum class MissionState {
  kIdle,
  kRunning,
  kPaused,
  kReturning,
  kCompleted,
  kFault,
  kEmergencyStopped,
};

enum class MissionEvent {
  kStart,
  kPause,
  kResume,
  kManualTakeover,
  kLinkLost,
  kEmergencyStop,
  kEmergencyStopReleased,
  kBatteryLow,
  kMissionComplete,
  kReturnComplete,
  kStepFailed,
  kStop,
  kResetFault,
};

struct Transition {
  MissionState from;
  MissionState to;
  bool accepted;
  const char * reason;
};

// Owns mission-level state only.  It is deliberately not a replacement for the
// hardware E-stop or for the command-level safety gate.
class MissionFsm {
public:
  [[nodiscard]] MissionState state() const;
  [[nodiscard]] bool autonomous_motion_allowed() const;
  Transition dispatch(MissionEvent event);

private:
  MissionState state_{MissionState::kIdle};
};

const char * to_string(MissionState state);
const char * to_string(MissionEvent event);

}  // namespace mission_manager
