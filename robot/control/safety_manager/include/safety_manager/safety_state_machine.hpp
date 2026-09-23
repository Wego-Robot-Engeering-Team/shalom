// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#pragma once

namespace safety_manager::core {

enum class State {
  Initializing,
  ControlledStop,
  Normal,
  EmergencyStopLatched,
  Fault,
};

// Preconditions documented as guards are checked by the Safety Manager before
// it emits InputsReady, EStopReleased, ClearFault, or ResumeRequested.
enum class Event {
  InputsReady,
  StopRequested,
  LinkLost,
  WatchdogTimeout,
  HealthFault,
  EmergencyStopEngaged,
  EmergencyStopReleased,
  ClearFault,
  ResumeRequested,
};

struct Transition {
  State from;
  State to;
  bool accepted;
  const char * reason;
};

class StateMachine {
public:
  [[nodiscard]] State state() const;
  [[nodiscard]] bool motion_permitted() const;
  [[nodiscard]] bool fault_latched() const;
  Transition dispatch(Event event);

private:
  Transition accept(State from, State to, const char * reason);
  Transition reject(const char * reason) const;

  State state_{State::Initializing};
  bool fault_latched_{false};
};

const char * to_string(State state);
const char * to_string(Event event);

}  // namespace safety_manager::core
