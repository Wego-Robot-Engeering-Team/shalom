// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#pragma once

#include "robot_sdk/types.hpp"
#include "robot_sdk/client.hpp"

#include <cmath>
#include <algorithm>
#include <cstdio>
#include <iomanip>
#include <locale>
#include <sstream>
#include <set>
#include <nlohmann/json.hpp>
#include <string>

namespace robot_sdk::api::detail {

inline bool finite(double value)
{
    return std::isfinite(value);
}

inline bool finite(const Pose2D &pose)
{
    return finite(pose.x) && finite(pose.y) && finite(pose.theta);
}

inline std::string invalid(std::string *err, const char *message)
{
    if (err)
        *err = message;
    return {};
}

inline std::string number(double value)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(17) << value;
    return out.str();
}

inline bool validId(const std::string &value)
{
    return !value.empty() && value.size() <= 96 && std::all_of(value.begin(), value.end(),
        [](unsigned char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_'; });
}

inline bool validText(const std::string &value, std::size_t maximum = 120)
{
    return !value.empty() && value.size() <= maximum && std::none_of(value.begin(), value.end(),
        [](char c) { return c == '\n' || c == '\r' || c == '\0'; });
}

inline std::string objectRequest(Client &client, const char *channel, const std::string &json,
                                 const char *field, std::uint64_t revision, const std::string &map,
                                 std::string *err)
{
    try {
        auto value = nlohmann::json::parse(json);
        if (!value.is_object()) return invalid(err, "value must be a JSON object");
        if (!value.contains("id") || !value["id"].is_string() || !validId(value["id"].get<std::string>()) ||
            !value.contains("name") || !value["name"].is_string() ||
            !validText(value["name"].get<std::string>(), std::string(field) == "preset" ? 80 : 120))
            return invalid(err, "invalid definition id or name");
        if (value.contains("description") && (!value["description"].is_string() ||
            (std::string(field) == "preset" && value["description"].get<std::string>().size() > 400)))
            return invalid(err, "invalid description");
        if (std::string(field) == "preset") {
            if (!value.contains("positions") || !value["positions"].is_array() || value["positions"].size() != 6 ||
                std::any_of(value["positions"].begin(), value["positions"].end(), [](const auto &v) {
                    return !v.is_number() || !finite(v.template get<double>()); }))
                return invalid(err, "preset requires six finite joint angles");
        } else {
            if (!value.contains("steps") || !value["steps"].is_array()) return invalid(err, "steps must be an array");
            std::set<std::string> ids;
            for (const auto &step : value["steps"]) {
                if (!step.is_object() || !step.contains("id") || !step["id"].is_string() ||
                    !validId(step["id"].get<std::string>()) || !ids.insert(step["id"].get<std::string>()).second ||
                    !step.contains("type") || !step["type"].is_string()) return invalid(err, "invalid step id or type");
                const auto type = step["type"].get<std::string>();
                if (type != "navigate" && type != "capture" && type != "arm_move" && type != "dock")
                    return invalid(err, "unsupported step type");
                const char *reference = type == "navigate" ? "location_id" : type == "capture" ? "preset" : type == "arm_move" ? "pose" : nullptr;
                if (reference && (!step.contains(reference) || !step[reference].is_string() || !validId(step[reference].get<std::string>())))
                    return invalid(err, "missing step reference");
            }
        }
        if (!map.empty()) {
            if (value.contains("map_id") && value["map_id"] != map) return invalid(err, "definition map differs from edit map");
            value["map_id"] = map;
        }
        return client.sendRequest(channel, nlohmann::json{{field, value}, {"expected_revision", revision}}.dump(), err);
    } catch (const std::exception &e) { if (err) *err = e.what(); return {}; }
}

inline std::string poseJson(const Pose2D &pose)
{
    return "\"x\":" + number(pose.x) + ",\"y\":" + number(pose.y)
           + ",\"theta\":" + number(pose.theta);
}

inline std::string escape(const std::string &value)
{
    std::string out;
    out.reserve(value.size());
    for (const unsigned char c : value) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char escaped[7];
                std::snprintf(escaped, sizeof escaped, "\\u%04x", unsigned(c));
                out += escaped;
            } else {
                out += static_cast<char>(c);
            }
        }
    }
    return out;
}

}  // namespace robot_sdk::api::detail
