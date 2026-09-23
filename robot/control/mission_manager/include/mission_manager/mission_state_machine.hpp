// Copyright (c) 2026 WeGo Robotics. All rights reserved.

#pragma once

namespace mission_manager::core {

// Mission lifecycle state defined by docs/control_architecture_contract.md.
// Safety and E-stop state deliberately belong to safety_manager, not here.
enum class State {
  Idle,
  Ready,
  Running,
  Pausing,
  Paused,
  Recovering,
  Returning,
  Completed,
  Failed,
};

// Guards such as capability, health, localization, and authority checks are
// evaluated by the Mission Manager before the corresponding event is emitted.
enum class Event {
  MissionConfigured,
  StartRequested,
  PauseRequested,
  ManualTakeover,
  LinkLost,
  SafetyStop,
  StopRequested,
  FatalStepFailure,
  MotionQuiesced,
  ResumeRequested,
  RecoveryReady,
  InspectionComplete,
  ReturnComplete,
  ResetRequested,
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
  [[nodiscard]] bool autonomous_motion_allowed() const;
  [[nodiscard]] State halt_target() const;
  [[nodiscard]] State resume_target() const;

  // This mission-level option is captured from the validated immutable plan.
  // It may only be changed while IDLE.
  void set_return_to_dock(bool enabled);
  [[nodiscard]] bool return_to_dock() const;

  Transition dispatch(Event event);

private:
  Transition accept(State from, State to, const char * reason);
  Transition reject(const char * reason) const;
  Transition begin_pausing(State from, State target, const char * reason);

  State state_{State::Idle};
  State halt_target_{State::Paused};
  State resume_target_{State::Running};
  bool return_to_dock_{false};
};

const char * to_string(State state);
const char * to_string(Event event);

}  // namespace mission_manager::core
