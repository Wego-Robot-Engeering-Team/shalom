// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include "api/detail.hpp"
#include "robot_sdk/api.hpp"

namespace robot_sdk {

std::string RobotApi::navigateTo(const Pose2D &goal, std::string *err)
{
    if (!api::detail::finite(goal))
        return api::detail::invalid(err, "goal must contain finite numbers");
    return client_.sendRequest("cmd/goto", "{\"x\":" + api::detail::number(goal.x)
                                         + ",\"y\":" + api::detail::number(goal.y)
                                         + ",\"theta\":" + api::detail::number(goal.theta) + "}", err);
}

std::string RobotApi::cancelNavigation(std::string *err)
{
    return client_.sendRequest("cmd/nav_cancel", "{}", err);
}

std::string RobotApi::pauseNavigation(std::string *err)
{
    return client_.sendRequest("cmd/nav_pause", "{}", err);
}

std::string RobotApi::resumeNavigation(std::string *err) { return client_.sendRequest("cmd/nav_resume", "{}", err, 5000); }
std::string RobotApi::trailSnapshot(std::string *err) { return client_.sendRequest("cmd/trail/snapshot", "{}", err); }
std::string RobotApi::setInitialPose(const Pose2D &pose, std::string *err) {
    if (!api::detail::finite(pose)) return api::detail::invalid(err, "initial pose must be finite");
    return client_.sendRequest("cmd/localization/initial_pose", "{" + api::detail::poseJson(pose) + "}", err);
}
std::string RobotApi::setSpeedLimits(double linear, double angular, std::string *err) {
    if (!api::detail::finite(linear) || !api::detail::finite(angular) || linear < 0.10 || linear > 0.60 || angular < 0.05 || angular > 0.80)
        return api::detail::invalid(err, "invalid speed limits");
    return client_.sendRequest("cmd/navigation/speed_limit", nlohmann::json{{"speed_limit_mps", linear},
        {"angular_speed_limit_rps", angular}}.dump(), err);
}
std::string RobotApi::setSpeedRanges(double minLinear, double maxLinear, double minAngular, double maxAngular, std::string *err) {
    if (!api::detail::finite(minLinear) || !api::detail::finite(maxLinear) || !api::detail::finite(minAngular) || !api::detail::finite(maxAngular) ||
        minLinear < 0.10 || minLinear > maxLinear || maxLinear > 0.60 || minAngular < 0.05 || minAngular > maxAngular || maxAngular > 0.80)
        return api::detail::invalid(err, "invalid speed ranges");
    return client_.sendRequest("cmd/navigation/speed_settings", nlohmann::json{{"min_speed_mps", minLinear},
        {"max_speed_mps", maxLinear}, {"min_angular_speed_rps", minAngular}, {"max_angular_speed_rps", maxAngular}}.dump(), err);
}

}  // namespace robot_sdk
