// Copyright (c) 2026 WeGo Robotics. All rights reserved.

#pragma once

#include "shalom/api/detail.hpp"
#include "shalom/client.hpp"

#include <optional>
#include <string>
#include <vector>

namespace shalom::api {

/// Camera-capture commands and commissioning-only arm commands.
class InspectionApi {
public:
    explicit InspectionApi(Client &client) : client_(client) {}

    std::string triggerCapture(const std::string &vehicleNumber = "UNKNOWN",
                               const std::string &carNumber = "00",
                               const std::string &pointId = "MANUAL",
                               std::optional<int> tagId = std::nullopt,
                               std::string *err = nullptr)
    {
        std::string json = "{\"vehicle_number\":\"" + detail::escape(vehicleNumber)
                           + "\",\"car_number\":\"" + detail::escape(carNumber)
                           + "\",\"point_id\":\"" + detail::escape(pointId) + "\"";
        if (tagId)
            json += ",\"tag_id\":" + std::to_string(*tagId);
        return client_.sendRequest("cmd/capture/trigger", json + '}', err);
    }

    /// Do not expose arm methods in an operator UI until the delivered FR3
    /// authority path has been approved.
    std::string armPreset(const std::string &name, std::string *err = nullptr)
    {
        if (name.empty())
            return detail::invalid(err, "arm preset must not be empty");
        return client_.sendRequest("cmd/arm/preset", "{\"name\":\"" + detail::escape(name) + "\"}", err);
    }

    std::string armJointGoal(const std::vector<double> &positions, std::string *err = nullptr)
    {
        if (positions.empty())
            return detail::invalid(err, "arm joint goal must not be empty");
        std::string values = "[";
        for (std::size_t i = 0; i < positions.size(); ++i) {
            if (!detail::finite(positions[i]))
                return detail::invalid(err, "arm joint goal must contain finite numbers");
            if (i)
                values += ',';
            values += detail::number(positions[i]);
        }
        return client_.sendRequest("cmd/arm/joint_goal", "{\"positions\":" + values + "]}", err);
    }

    std::string armStop(std::string *err = nullptr)
    {
        return client_.sendRequest("cmd/arm/stop", "{}", err);
    }

protected:
    Client &client_;
};

}  // namespace shalom::api
