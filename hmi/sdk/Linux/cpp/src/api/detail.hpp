// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#pragma once

#include "shalom/types.hpp"

#include <cmath>
#include <cstdio>
#include <string>

namespace shalom::api::detail {

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
    char out[64];
    std::snprintf(out, sizeof out, "%.12g", value);
    return out;
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

}  // namespace shalom::api::detail
