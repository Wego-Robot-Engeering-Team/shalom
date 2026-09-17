// Copyright (c) 2026 WeGo Robotics. All rights reserved.

#pragma once

// Stable public facade.  Domain implementations live in shalom/api/; this
// class deliberately keeps customer call sites flat and language-neutral.

#include "shalom/api/configuration.hpp"
#include "shalom/api/inspection.hpp"
#include "shalom/api/mission.hpp"
#include "shalom/api/navigation.hpp"
#include "shalom/api/safety.hpp"

#include <string>

namespace shalom {

/// Public robot command API. It owns no socket: keep Client::run() active for
/// heartbeat, responses and state events. A returned request id confirms only
/// bridge acceptance; observe state frames for the physical result.
class RobotApi : public api::SafetyApi,
                 public api::NavigationApi,
                 public api::MissionApi,
                 public api::ConfigurationApi,
                 public api::InspectionApi {
public:
    explicit RobotApi(Client &client)
        : SafetyApi(client), NavigationApi(client), MissionApi(client),
          ConfigurationApi(client), InspectionApi(client), client_(client)
    {
    }

    /// Advanced configuration and commissioning channels use this escape
    /// hatch. The payload must be a JSON object specified in command.md.
    std::string request(const std::string &channel, const std::string &payloadJson = "{}",
                        std::string *err = nullptr)
    {
        return client_.sendRequest(channel, payloadJson, err);
    }

private:
    Client &client_;
};

}  // namespace shalom
