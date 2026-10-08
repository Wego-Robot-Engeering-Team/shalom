// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#pragma once

#include "mission_manager/bt/status.hpp"

namespace mission_manager::bt {

class StairRuntime {
public:
  virtual ~StairRuntime() = default;
  virtual Status confirm_stair_route_safe() = 0;
  virtual Status engage_stair_mode() = 0;
  virtual Status traverse_stairs() = 0;
  virtual Status confirm_stair_exit() = 0;
  virtual void halt_stair_motion() = 0;
};

// Dedicated mobility sequence. Nav2 is not asked to perform the stair traverse.
class StairBt {
public:
  Status tick(StairRuntime & runtime);
  void halt(StairRuntime & runtime);

private:
  enum class Step { ConfirmRoute, EngageMode, Traverse, ConfirmExit };
  Step next_step_{Step::ConfirmRoute};
};

}  // namespace mission_manager::bt
