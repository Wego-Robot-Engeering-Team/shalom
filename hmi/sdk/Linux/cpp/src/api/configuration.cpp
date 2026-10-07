// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary
#include "api/detail.hpp"
#include "robot_sdk/api.hpp"
#include <set>

namespace robot_sdk {
using json = nlohmann::json;
std::string RobotApi::listMaps(std::string *err) { return client_.sendRequest("cmd/maps/list", "{}", err); }
std::string RobotApi::selectMap(const std::string &id, std::string *err) {
    if (!api::detail::validText(id)) return api::detail::invalid(err, "invalid map id");
    return client_.sendRequest("cmd/maps/select", json{{"id",id}}.dump(), err, 10000);
}
std::string RobotApi::renameMap(const std::string &id, const std::string &name, std::string *err) {
    if (!api::detail::validText(id) || !api::detail::validText(name)) return api::detail::invalid(err, "invalid map id/name");
    return client_.sendRequest("cmd/maps/rename", json{{"id",id},{"name",name}}.dump(), err, 5000);
}
std::string RobotApi::deleteMap(const std::string &id, std::string *err) {
    if (!api::detail::validText(id)) return api::detail::invalid(err, "invalid map id");
    return client_.sendRequest("cmd/maps/delete", json{{"id",id}}.dump(), err, 5000);
}
std::string RobotApi::setDefaultMap(const std::string &id, std::string *err) {
    if (!id.empty() && !api::detail::validText(id)) return api::detail::invalid(err, "invalid map id");
    return client_.sendRequest("cmd/maps/set_default", json{{"id",id}}.dump(), err);
}
std::string RobotApi::setPowerPolicy(int returnAt, int departAt, std::string *err) {
    if (returnAt < 0 || departAt > 100 || returnAt >= departAt) return api::detail::invalid(err, "require 0 <= return < depart <= 100");
    return client_.sendRequest("cmd/power/policy", json{{"return_at",returnAt},{"depart_at",departAt}}.dump(), err);
}
namespace {
std::string replaceCatalog(Client &client, const char *channel, const char *field, const char *expectedField,
                           const json &values, const std::string &map, const std::string &expectedJson, std::string *err) {
    if (!api::detail::validText(map)) return api::detail::invalid(err, "invalid edit map id");
    try {
        auto expected = json::parse(expectedJson);
        if (!expected.is_array() || std::any_of(expected.begin(), expected.end(), [](const json &v) { return !v.is_object(); }))
            return api::detail::invalid(err, "expected catalog must be an array of objects");
        if (std::string(field) == "points") {
            for (auto &item : expected) for (const char *key : {"status","captured_from","localization_ok","tag_id","kind"}) item.erase(key);
        }
        return client.sendRequest(channel, json{{field,values},{expectedField,expected},{"map_id",map}}.dump(), err);
    } catch (const std::exception &e) { if (err) *err = e.what(); return {}; }
}
}
std::string RobotApi::setWaypoints(const std::vector<Waypoint> &points, const std::string &map,
                                   const std::string &expected, std::string *err) {
    auto values = json::array();
    std::set<std::string> ids;
    for (const auto &point : points) {
        const auto name = point.name.empty() ? point.id : point.name;
        if (!api::detail::validText(point.id) || !api::detail::validText(name) || !api::detail::finite(point.pose) || !ids.insert(point.id).second)
            return api::detail::invalid(err, "invalid or duplicate waypoint");
        values.push_back(json{{"id",point.id},{"name",name},{"x",point.pose.x},{"y",point.pose.y},
                              {"theta",point.pose.theta},{"description",point.description}});
    }
    return replaceCatalog(client_, "cmd/waypoints/set", "points", "expected_points", values, map, expected, err);
}
std::string RobotApi::setLocations(const std::vector<Location> &locations, const std::string &map,
                                   const std::string &expected, std::string *err) {
    auto values = json::array(); std::set<std::string> kinds;
    for (const auto &location : locations) {
        if ((location.kind != "dock" && location.kind != "home") || !kinds.insert(location.kind).second || !api::detail::finite(location.pose))
            return api::detail::invalid(err, "invalid or duplicate home/dock");
        values.push_back(json{{"kind",location.kind},{"x",location.pose.x},{"y",location.pose.y},{"theta",location.pose.theta}});
    }
    return replaceCatalog(client_, "cmd/locations/set", "locations", "expected_locations", values, map, expected, err);
}
std::string RobotApi::setMarkers(const std::vector<Marker> &markers, const std::string &map,
                                 const std::string &expected, std::string *err) {
    auto values = json::array(); std::set<int> ids;
    for (const auto &marker : markers) {
        if (marker.id < 0 || marker.id > 100000 || !ids.insert(marker.id).second || !api::detail::finite(marker.pose) ||
            !api::detail::finite(marker.z) || std::abs(marker.pose.theta) > 3.141592653589793 + 1e-6)
            return api::detail::invalid(err, "invalid or duplicate marker");
        values.push_back(json{{"id",marker.id},{"x",marker.pose.x},{"y",marker.pose.y},{"z",marker.z},
                              {"yaw",marker.pose.theta},{"description",marker.description}});
    }
    return replaceCatalog(client_, "cmd/markers/set", "markers", "expected_markers", values, map, expected, err);
}
}  // namespace robot_sdk
