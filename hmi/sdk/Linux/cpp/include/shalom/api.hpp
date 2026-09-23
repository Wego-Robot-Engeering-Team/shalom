// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#pragma once

#include "shalom/client.hpp"
#include "shalom/export.hpp"
#include "shalom/types.hpp"

#include <optional>
#include <string>
#include <vector>

namespace shalom {

/// Stable public command facade. Implementation is grouped by domain in the
/// SDK binary; customers use this single, flat interface.
class SHALOM_SDK_API RobotApi {
public:
    explicit RobotApi(Client &client);

    std::string emergencyStop(std::string *err = nullptr);
    std::string releaseEmergencyStop(std::string *err = nullptr);
    std::string setMode(const std::string &mode, std::string *err = nullptr);

    std::string navigateTo(const Pose2D &goal, std::string *err = nullptr);
    std::string cancelNavigation(std::string *err = nullptr);
    bool publishVelocity(const Twist2D &velocity, std::string *err = nullptr);

    std::string startMission(std::string *err = nullptr);
    std::string pauseMission(std::string *err = nullptr);
    std::string resumeMission(std::string *err = nullptr);
    std::string stopMission(std::string *err = nullptr);

    std::string listMaps(std::string *err = nullptr);
    std::string selectMap(const std::string &mapId, std::string *err = nullptr);
    std::string setPowerPolicy(int returnAt, int departAt, std::string *err = nullptr);
    std::string setWaypoints(const std::vector<Waypoint> &points, std::string *err = nullptr);
    std::string setLocations(const std::vector<Location> &locations, std::string *err = nullptr);
    std::string setMarkers(const std::vector<Marker> &markers, std::string *err = nullptr);

    std::string triggerCapture(const std::string &vehicleNumber = "UNKNOWN",
                               const std::string &carNumber = "00",
                               const std::string &pointId = "MANUAL",
                               std::optional<int> tagId = std::nullopt,
                               std::string *err = nullptr);
    std::string armPreset(const std::string &name, std::string *err = nullptr);
    std::string armJointGoal(const std::vector<double> &positions, std::string *err = nullptr);
    std::string armStop(std::string *err = nullptr);

    /// Escape hatch for documented advanced channels only.
    std::string request(const std::string &channel, const std::string &payloadJson = "{}",
                        std::string *err = nullptr);

private:
    Client &client_;
};

}  // namespace shalom
