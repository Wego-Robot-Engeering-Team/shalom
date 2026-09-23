// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include "mission_manager/bt/status.hpp"

namespace mission_manager::bt {

const char * to_string(Status status) {
  switch (status) {
    case Status::Running: return "running";
    case Status::Success: return "success";
    case Status::Failure: return "failure";
  }
  return "unknown";
}

}  // namespace mission_manager::bt
