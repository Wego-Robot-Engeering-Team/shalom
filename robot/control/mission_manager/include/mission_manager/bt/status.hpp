// Copyright (c) 2026 WeGo Robotics. All rights reserved.

#pragma once

namespace mission_manager::bt {

enum class Status { kRunning, kSuccess, kFailure };

const char * to_string(Status status);

}  // namespace mission_manager::bt
