// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include "robot/RobotLink.h"

#include <QMetaType>

namespace hmi::robot {
namespace {

const int kRegistered = [] {
    qRegisterMetaType<hmi::robot::Telemetry>("hmi::robot::Telemetry");
    qRegisterMetaType<hmi::robot::MissionState>("hmi::robot::MissionState");
    qRegisterMetaType<hmi::robot::DriveMode>("hmi::robot::DriveMode");
    return 0;
}();

}  // namespace
}  // namespace hmi::robot
