// Copyright (c) 2026 WeGo Robotics. All rights reserved.

#include "mission_manager/bt/status.hpp"

namespace mission_manager::bt {

const char * to_string(Status status) {
  switch (status) {
    case Status::kRunning: return "running";
    case Status::kSuccess: return "success";
    case Status::kFailure: return "failure";
  }
  return "unknown";
}

}  // namespace mission_manager::bt
