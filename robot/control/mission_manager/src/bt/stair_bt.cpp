// Copyright (c) 2026 WeGo Robotics. All rights reserved.

#include "mission_manager/bt/stair_bt.hpp"

namespace mission_manager::bt {

Status StairBt::tick(StairRuntime & runtime) {
  while (true) {
    Status status = Status::Failure;
    switch (next_step_) {
      case Step::ConfirmRoute:
        status = runtime.confirm_stair_route_safe();
        if (status == Status::Success) next_step_ = Step::EngageMode;
        break;
      case Step::EngageMode:
        status = runtime.engage_stair_mode();
        if (status == Status::Success) next_step_ = Step::Traverse;
        break;
      case Step::Traverse:
        status = runtime.traverse_stairs();
        if (status == Status::Success) next_step_ = Step::ConfirmExit;
        break;
      case Step::ConfirmExit:
        status = runtime.confirm_stair_exit();
        if (status == Status::Success) next_step_ = Step::ConfirmRoute;
        break;
    }
    if (status != Status::Success || next_step_ == Step::ConfirmRoute) return status;
  }
}

void StairBt::halt(StairRuntime & runtime) {
  runtime.halt_stair_motion();
  next_step_ = Step::ConfirmRoute;
}

}  // namespace mission_manager::bt
