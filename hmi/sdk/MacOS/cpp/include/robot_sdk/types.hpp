// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#pragma once

// Public value types shared by the C++ RobotApi command facade.

#include <string>

namespace robot_sdk {

struct Pose2D {
    double x = 0.0;
    double y = 0.0;
    double theta = 0.0;
};

struct Waypoint {
    std::string id;
    Pose2D pose;
    std::string name;
    std::string description;
};

struct Location {
    std::string kind;  // "dock" or "home"
    Pose2D pose;
};

struct Marker {
    int id = 0;
    Pose2D pose; // x/y in map; theta is the tag front-face normal yaw (rad).
    double z = 0.0;
    std::string description;
};

}  // namespace robot_sdk
