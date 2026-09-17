// Copyright (c) 2026 WeGo Robotics. All rights reserved.

#include "mission_manager/bt/nav2_bt.hpp"

namespace mission_manager::bt {

Status Nav2Bt::tick(Nav2Runtime & runtime, const std::string & goal_id) {
  if (!active_goal_id_.empty() && active_goal_id_ != goal_id) {
    return Status::kFailure;
  }
  active_goal_id_ = goal_id;
  const Status status = runtime.navigate_to(goal_id);
  if (status != Status::kRunning) {
    active_goal_id_.clear();
  }
  return status;
}

void Nav2Bt::halt(Nav2Runtime & runtime) {
  if (!active_goal_id_.empty()) {
    runtime.cancel_navigation();
    active_goal_id_.clear();
  }
}

}  // namespace mission_manager::bt
