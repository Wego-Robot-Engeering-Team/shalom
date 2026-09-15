#include <cstdlib>

#include "safety_manager/safety_fsm.hpp"

namespace {

void check(bool condition) {
  if (!condition) std::exit(1);
}

}  // namespace

int main() {
  using safety_manager::SafetyEvent;
  using safety_manager::SafetyFsm;
  using safety_manager::SafetyState;

  SafetyFsm fsm;
  check(fsm.motion_permitted());
  fsm.dispatch(SafetyEvent::kRequestStop);
  check(fsm.state() == SafetyState::kControlledStop);
  check(!fsm.motion_permitted());
  fsm.dispatch(SafetyEvent::kResume);
  check(fsm.state() == SafetyState::kNormal);
  fsm.dispatch(SafetyEvent::kEmergencyStopPressed);
  fsm.dispatch(SafetyEvent::kEmergencyStopReleased);
  check(fsm.state() == SafetyState::kControlledStop);
  fsm.dispatch(SafetyEvent::kHealthFault);
  check(fsm.state() == SafetyState::kFault);
  fsm.dispatch(SafetyEvent::kClearFault);
  check(fsm.state() == SafetyState::kControlledStop);
  return 0;
}
