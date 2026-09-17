// Copyright (c) 2026 WeGo Robotics. All rights reserved.

#pragma once

// Public value types shared by the C++ RobotApi command facade.

#include <string>

namespace shalom {

struct Pose2D {
    double x = 0.0;
    double y = 0.0;
    double theta = 0.0;
};

struct Twist2D {
    double vx = 0.0;
    double vy = 0.0;
    double wz = 0.0;
};

struct Waypoint {
    std::string id;
    Pose2D pose;
};

struct Location {
    std::string kind;  // "dock" or "home"
    Pose2D pose;
};

struct Marker {
    int id = 0;
    Pose2D pose;
};

}  // namespace shalom
