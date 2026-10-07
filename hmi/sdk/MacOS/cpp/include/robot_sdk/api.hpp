// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#pragma once

#include "robot_sdk/client.hpp"
#include "robot_sdk/export.hpp"
#include "robot_sdk/types.hpp"

#include <optional>
#include <string>
#include <vector>

namespace robot_sdk {

/// Stable public command facade. Implementation is grouped by domain in the
/// SDK binary; customers use this single, flat interface.
class ROBOT_SDK_API RobotApi {
public:
    explicit RobotApi(Client &client);

    std::string emergencyStop(std::string *err = nullptr);
    std::string releaseEmergencyStop(std::string *err = nullptr);
    std::string setMode(const std::string &mode, std::string *err = nullptr);

    std::string navigateTo(const Pose2D &goal, std::string *err = nullptr);
    std::string cancelNavigation(std::string *err = nullptr);
    std::string pauseNavigation(std::string *err = nullptr);
    std::string resumeNavigation(std::string *err = nullptr);
    std::string setInitialPose(const Pose2D &pose, std::string *err = nullptr);
    std::string setSpeedLimits(double linearMps, double angularRps, std::string *err = nullptr);
    std::string setSpeedRanges(double minLinearMps, double maxLinearMps,
                              double minAngularRps, double maxAngularRps, std::string *err = nullptr);
    std::string trailSnapshot(std::string *err = nullptr);

    std::string startMission(const std::string &missionId, std::string *err = nullptr);
    std::string pauseMission(std::string *err = nullptr);
    std::string resumeMission(std::string *err = nullptr);
    std::string stopMission(std::string *err = nullptr);
    std::string returnToDock(std::string *err = nullptr);
    std::string listMissions(std::string *err = nullptr);
    std::string saveMission(const std::string &missionJson, const std::string &mapId,
                            std::uint64_t expectedRevision = 0, std::string *err = nullptr);
    std::string archiveMission(const std::string &missionId, const std::string &mapId,
                               std::uint64_t expectedRevision, std::string *err = nullptr);

    std::string listMaps(std::string *err = nullptr);
    std::string selectMap(const std::string &mapId, std::string *err = nullptr);
    std::string renameMap(const std::string &mapId, const std::string &name, std::string *err = nullptr);
    std::string deleteMap(const std::string &mapId, std::string *err = nullptr);
    std::string setDefaultMap(const std::string &mapId, std::string *err = nullptr);
    std::string setPowerPolicy(int returnAt, int departAt, std::string *err = nullptr);
    std::string setWaypoints(const std::vector<Waypoint> &points, const std::string &mapId,
                             const std::string &expectedPointsJson, std::string *err = nullptr);
    std::string setLocations(const std::vector<Location> &locations, const std::string &mapId,
                             const std::string &expectedLocationsJson, std::string *err = nullptr);
    std::string setMarkers(const std::vector<Marker> &markers, const std::string &mapId,
                           const std::string &expectedMarkersJson, std::string *err = nullptr);
    std::string setBasePosture(const std::string &posture, bool confirm = false, std::string *err = nullptr);

    std::string triggerCapture(const std::string &vehicleNumber, const std::string &trainNumber,
                               const std::string &carNumber, const std::string &pointId,
                               std::optional<int> tagId = std::nullopt,
                               std::string *err = nullptr);
    std::string armPreset(const std::string &name, std::string *err = nullptr);
    std::string armJointGoal(const std::vector<double> &positions, std::string *err = nullptr);
    std::string armStop(std::string *err = nullptr);
    std::string listArmPoses(std::string *err = nullptr);
    std::string saveArmPose(const std::string &presetJson, std::string *err = nullptr);
    std::string updateArmPose(const std::string &presetJson, std::uint64_t expectedRevision,
                             std::string *err = nullptr);
    std::string archiveArmPose(const std::string &poseId, std::uint64_t expectedRevision,
                              std::string *err = nullptr);

    /// Escape hatch for documented advanced channels only.
    std::string request(const std::string &channel, const std::string &payloadJson = "{}",
                        std::string *err = nullptr);

private:
    Client &client_;
};

}  // namespace robot_sdk
