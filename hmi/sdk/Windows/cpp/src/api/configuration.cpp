// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include "api/detail.hpp"
#include "shalom/api.hpp"

namespace shalom {

std::string RobotApi::listMaps(std::string *err)
{
    return client_.sendRequest("cmd/maps/list", "{}", err);
}

std::string RobotApi::selectMap(const std::string &mapId, std::string *err)
{
    if (mapId.empty())
        return api::detail::invalid(err, "map id must not be empty");
    return client_.sendRequest("cmd/maps/select", "{\"id\":\"" + api::detail::escape(mapId) + "\"}", err);
}

std::string RobotApi::setPowerPolicy(int returnAt, int departAt, std::string *err)
{
    if (returnAt < 0 || returnAt > 100 || departAt < 0 || departAt > 100)
        return api::detail::invalid(err, "power policy values must be percentages from 0 to 100");
    return client_.sendRequest("cmd/power/policy", "{\"return_at\":" + std::to_string(returnAt)
                                               + ",\"depart_at\":" + std::to_string(departAt) + "}", err);
}

std::string RobotApi::setWaypoints(const std::vector<Waypoint> &points, std::string *err)
{
    std::string json = "{\"points\":[";
    for (std::size_t i = 0; i < points.size(); ++i) {
        if (!api::detail::finite(points[i].pose))
            return api::detail::invalid(err, "waypoints must contain finite poses");
        if (i)
            json += ',';
        json += "{\"id\":\"" + api::detail::escape(points[i].id) + "\"," + api::detail::poseJson(points[i].pose) + '}';
    }
    return client_.sendRequest("cmd/waypoints/set", json + "]}", err);
}

std::string RobotApi::setLocations(const std::vector<Location> &locations, std::string *err)
{
    std::string json = "{\"locations\":[";
    for (std::size_t i = 0; i < locations.size(); ++i) {
        if ((locations[i].kind != "dock" && locations[i].kind != "home")
            || !api::detail::finite(locations[i].pose))
            return api::detail::invalid(err, "locations require dock/home and finite poses");
        if (i)
            json += ',';
        json += "{\"kind\":\"" + locations[i].kind + "\"," + api::detail::poseJson(locations[i].pose) + '}';
    }
    return client_.sendRequest("cmd/locations/set", json + "]}", err);
}

std::string RobotApi::setMarkers(const std::vector<Marker> &markers, std::string *err)
{
    std::string json = "{\"markers\":[";
    for (std::size_t i = 0; i < markers.size(); ++i) {
        if (!api::detail::finite(markers[i].pose))
            return api::detail::invalid(err, "markers must contain finite poses");
        if (i)
            json += ',';
        json += "{\"id\":" + std::to_string(markers[i].id) + ',' + api::detail::poseJson(markers[i].pose) + '}';
    }
    return client_.sendRequest("cmd/markers/set", json + "]}", err);
}

}  // namespace shalom
