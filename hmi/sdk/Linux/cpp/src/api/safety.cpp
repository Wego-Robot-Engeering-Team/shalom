// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include "shalom/api.hpp"

namespace shalom {

RobotApi::RobotApi(Client &client) : client_(client) {}

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

}  // namespace shalom
