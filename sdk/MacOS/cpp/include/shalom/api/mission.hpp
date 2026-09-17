// Copyright (c) 2026 WeGo Robotics. All rights reserved.

#pragma once

#include "shalom/client.hpp"

#include <string>

namespace shalom::api {

/// Mission lifecycle commands.
class MissionApi {
public:
    explicit MissionApi(Client &client) : client_(client) {}

    std::string startMission(std::string *err = nullptr)
    {
        return client_.sendRequest("cmd/mission/start", "{}", err);
    }
    std::string pauseMission(std::string *err = nullptr)
    {
        return client_.sendRequest("cmd/mission/pause", "{}", err);
    }
    std::string resumeMission(std::string *err = nullptr)
    {
        return client_.sendRequest("cmd/mission/resume", "{}", err);
    }
    std::string stopMission(std::string *err = nullptr)
    {
        return client_.sendRequest("cmd/mission/stop", "{}", err);
    }

protected:
    Client &client_;
};

}  // namespace shalom::api
