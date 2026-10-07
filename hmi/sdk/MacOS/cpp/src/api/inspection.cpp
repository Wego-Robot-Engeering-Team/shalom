// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include "api/detail.hpp"
#include "robot_sdk/api.hpp"

namespace robot_sdk {

std::string RobotApi::triggerCapture(const std::string &vehicleNumber, const std::string &trainNumber, const std::string &carNumber,
                                     const std::string &pointId, std::optional<int> tagId,
                                     std::string *err)
{
    if (!api::detail::validText(vehicleNumber) || !api::detail::validText(trainNumber) ||
        !api::detail::validText(carNumber) || !api::detail::validText(pointId) || (tagId && (*tagId < 0 || *tagId > 100000)))
        return api::detail::invalid(err, "invalid capture identifiers");
    std::string json = "{\"vehicle_number\":\"" + api::detail::escape(vehicleNumber)
                       + "\",\"train_number\":\"" + api::detail::escape(trainNumber)
                       + "\",\"car_number\":\"" + api::detail::escape(carNumber)
                       + "\",\"point_id\":\"" + api::detail::escape(pointId) + "\"";
    if (tagId)
        json += ",\"tag_id\":" + std::to_string(*tagId);
    return client_.sendRequest("cmd/capture/trigger", json + '}', err);
}

std::string RobotApi::armPreset(const std::string &name, std::string *err)
{
    if (name.empty())
        return api::detail::invalid(err, "arm preset must not be empty");
    return client_.sendRequest("cmd/arm/preset", "{\"name\":\"" + api::detail::escape(name) + "\"}", err);
}

std::string RobotApi::armJointGoal(const std::vector<double> &positions, std::string *err)
{
    if (positions.size() != 6)
        return api::detail::invalid(err, "arm joint goal requires six angles in radians");
    std::string values = "[";
    for (std::size_t i = 0; i < positions.size(); ++i) {
        if (!api::detail::finite(positions[i]))
            return api::detail::invalid(err, "arm joint goal must contain finite numbers");
        if (i)
            values += ',';
        values += api::detail::number(positions[i]);
    }
    return client_.sendRequest("cmd/arm/joint_goal", "{\"positions\":" + values + "]}", err);
}

std::string RobotApi::armStop(std::string *err)
{
    return client_.sendRequest("cmd/arm/stop", "{}", err);
}

std::string RobotApi::listArmPoses(std::string *err) { return client_.sendRequest("cmd/arm/pose_presets/list", "{}", err); }
std::string RobotApi::saveArmPose(const std::string &presetJson, std::string *err) {
    return api::detail::objectRequest(client_, "cmd/arm/pose_presets/save", presetJson, "preset", 0, {}, err);
}
std::string RobotApi::updateArmPose(const std::string &presetJson, std::uint64_t revision, std::string *err) {
    if (!revision) return api::detail::invalid(err, "expected revision must be positive");
    return api::detail::objectRequest(client_, "cmd/arm/pose_presets/update", presetJson, "preset", revision, {}, err);
}
std::string RobotApi::archiveArmPose(const std::string &id, std::uint64_t revision, std::string *err) {
    if (!api::detail::validId(id) || !revision) return api::detail::invalid(err, "invalid pose id or revision");
    return client_.sendRequest("cmd/arm/pose_presets/archive", nlohmann::json{{"id", id}, {"expected_revision", revision}}.dump(), err);
}

}  // namespace robot_sdk
