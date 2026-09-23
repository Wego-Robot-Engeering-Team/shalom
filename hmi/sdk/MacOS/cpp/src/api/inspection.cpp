// Copyright (c) 2026 WeGo Robotics. All rights reserved.

#include "api/detail.hpp"
#include "shalom/api.hpp"

namespace shalom {

std::string RobotApi::triggerCapture(const std::string &vehicleNumber, const std::string &carNumber,
                                     const std::string &pointId, std::optional<int> tagId,
                                     std::string *err)
{
    std::string json = "{\"vehicle_number\":\"" + api::detail::escape(vehicleNumber)
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
    if (positions.empty())
        return api::detail::invalid(err, "arm joint goal must not be empty");
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

}  // namespace shalom
