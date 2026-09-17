// Copyright (c) 2026 WeGo Robotics. All rights reserved.

#pragma once

#include "shalom/client.hpp"

#include <string>

namespace shalom::api {

/// Emergency-stop and operating-mode commands.
class SafetyApi {
public:
    explicit SafetyApi(Client &client) : client_(client) {}

    std::string emergencyStop(std::string *err = nullptr)
    {
        return client_.sendRequest("cmd/estop", "{}", err);
    }

    std::string releaseEmergencyStop(std::string *err = nullptr)
    {
        return client_.sendRequest("cmd/estop_release", "{}", err);
    }

    std::string setMode(const std::string &mode, std::string *err = nullptr)
    {
        if (mode != "auto" && mode != "manual") {
            if (err)
                *err = "mode must be auto or manual";
            return {};
        }
        return client_.sendRequest("cmd/mode", "{\"mode\":\"" + mode + "\"}", err);
    }

protected:
    Client &client_;
};

}  // namespace shalom::api
