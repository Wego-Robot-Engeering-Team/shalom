// Copyright (c) 2026 WeGo Robotics. All rights reserved.

#include "mission_manager/bt/stair_bt.hpp"

namespace mission_manager::bt {

Status StairBt::tick(StairRuntime & runtime) {
  while (true) {
    Status status = Status::kFailure;
    switch (next_step_) {
      case Step::kConfirmRoute:
        status = runtime.confirm_stair_route_safe();
        if (status == Status::kSuccess) next_step_ = Step::kEngageMode;
        break;
      case Step::kEngageMode:
        status = runtime.engage_stair_mode();
        if (status == Status::kSuccess) next_step_ = Step::kTraverse;
        break;
      case Step::kTraverse:
        status = runtime.traverse_stairs();
        if (status == Status::kSuccess) next_step_ = Step::kConfirmExit;
        break;
      case Step::kConfirmExit:
        status = runtime.confirm_stair_exit();
        if (status == Status::kSuccess) next_step_ = Step::kConfirmRoute;
        break;
    }
    if (status != Status::kSuccess || next_step_ == Step::kConfirmRoute) return status;
  }
}

void StairBt::halt(StairRuntime & runtime) {
  runtime.halt_stair_motion();
  next_step_ = Step::kConfirmRoute;
}

}  // namespace mission_manager::bt
