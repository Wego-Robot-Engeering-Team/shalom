// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include "robot_sdk/api.hpp"
#include "api/detail.hpp"

namespace robot_sdk {

RobotApi::RobotApi(Client &client) : client_(client) {}

std::string RobotApi::setBasePosture(const std::string &posture, bool confirm, std::string *err) {
    if ((posture != "stand_up" && posture != "stand_down" && posture != "balance_stand" &&
         posture != "recovery_stand" && posture != "damp") || (posture == "damp" && !confirm))
        return api::detail::invalid(err, "invalid posture or missing damp confirmation");
    return client_.sendRequest("cmd/base/posture", nlohmann::json{{"posture",posture},{"confirm",confirm}}.dump(), err, 5000);
}

std::string RobotApi::emergencyStop(std::string *err)
{
    return client_.sendRequest("cmd/estop", "{}", err);
}

std::string RobotApi::releaseEmergencyStop(std::string *err)
{
    return client_.sendRequest("cmd/estop_release", "{}", err);
}

std::string RobotApi::setMode(const std::string &mode, std::string *err)
{
    if (mode != "auto" && mode != "manual") {
        if (err)
            *err = "mode must be auto or manual";
        return {};
    }
    return client_.sendRequest("cmd/mode", "{\"mode\":\"" + mode + "\"}", err);
}

std::string RobotApi::request(const std::string &channel, const std::string &payloadJson,
                              std::string *err)
{
    return client_.sendRequest(channel, payloadJson, err);
}

}  // namespace robot_sdk
