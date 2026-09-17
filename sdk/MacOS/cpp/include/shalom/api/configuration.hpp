// Copyright (c) 2026 WeGo Robotics. All rights reserved.

#pragma once

#include "shalom/api/detail.hpp"
#include "shalom/client.hpp"

#include <string>
#include <vector>

namespace shalom::api {

/// Persistent map, power-policy and site-configuration commands.
class ConfigurationApi {
public:
    explicit ConfigurationApi(Client &client) : client_(client) {}

    std::string listMaps(std::string *err = nullptr)
    {
        return client_.sendRequest("cmd/maps/list", "{}", err);
    }

    std::string selectMap(const std::string &mapId, std::string *err = nullptr)
    {
        if (mapId.empty())
            return detail::invalid(err, "map id must not be empty");
        return client_.sendRequest("cmd/maps/select", "{\"id\":\"" + detail::escape(mapId) + "\"}", err);
    }

    std::string setPowerPolicy(int returnAt, int departAt, std::string *err = nullptr)
    {
        if (returnAt < 0 || returnAt > 100 || departAt < 0 || departAt > 100)
            return detail::invalid(err, "power policy values must be percentages from 0 to 100");
        return client_.sendRequest("cmd/power/policy", "{\"return_at\":" + std::to_string(returnAt)
                                                       + ",\"depart_at\":" + std::to_string(departAt) + "}", err);
    }

    std::string setWaypoints(const std::vector<Waypoint> &points, std::string *err = nullptr)
    {
        std::string json = "{\"points\":[";
        for (std::size_t i = 0; i < points.size(); ++i) {
            if (!detail::finite(points[i].pose))
                return detail::invalid(err, "waypoints must contain finite poses");
            if (i)
                json += ',';
            json += "{\"id\":\"" + detail::escape(points[i].id) + "\"," + detail::poseJson(points[i].pose) + '}';
        }
        return client_.sendRequest("cmd/waypoints/set", json + "]}", err);
    }

    std::string setLocations(const std::vector<Location> &locations, std::string *err = nullptr)
    {
        std::string json = "{\"locations\":[";
        for (std::size_t i = 0; i < locations.size(); ++i) {
            if ((locations[i].kind != "dock" && locations[i].kind != "home")
                || !detail::finite(locations[i].pose))
                return detail::invalid(err, "locations require dock/home and finite poses");
            if (i)
                json += ',';
            json += "{\"kind\":\"" + locations[i].kind + "\"," + detail::poseJson(locations[i].pose) + '}';
        }
        return client_.sendRequest("cmd/locations/set", json + "]}", err);
    }

    std::string setMarkers(const std::vector<Marker> &markers, std::string *err = nullptr)
    {
        std::string json = "{\"markers\":[";
        for (std::size_t i = 0; i < markers.size(); ++i) {
            if (!detail::finite(markers[i].pose))
                return detail::invalid(err, "markers must contain finite poses");
            if (i)
                json += ',';
            json += "{\"id\":" + std::to_string(markers[i].id) + ',' + detail::poseJson(markers[i].pose) + '}';
        }
        return client_.sendRequest("cmd/markers/set", json + "]}", err);
    }

protected:
    Client &client_;
};

}  // namespace shalom::api
