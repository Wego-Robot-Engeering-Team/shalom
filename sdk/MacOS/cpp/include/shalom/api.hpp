// Copyright (c) 2026 WeGo Robotics. All rights reserved.

#pragma once

// Typed convenience layer over Client's non-blocking request API.
//
// A request id is returned immediately.  The application receives the matching
// `res` frame from Client::run() and must use state/* frames to observe the
// actual result.  A successful cmd/goto response means Nav2 accepted the goal,
// not that the robot has reached it.

#include "shalom/client.hpp"
#include "shalom/types.hpp"

#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace shalom {

/// Public robot command API.  It owns no socket: keep the Client running to
/// maintain the required heartbeat and to receive command responses.
class RobotApi {
public:
    explicit RobotApi(Client &client) : client_(client) {}

    std::string emergencyStop(std::string *err = nullptr)
    {
        return request("cmd/estop", "{}", err);
    }

    std::string releaseEmergencyStop(std::string *err = nullptr)
    {
        return request("cmd/estop_release", "{}", err);
    }

    std::string setMode(const std::string &mode, std::string *err = nullptr)
    {
        if (mode != "auto" && mode != "manual") {
            if (err)
                *err = "mode must be auto or manual";
            return {};
        }
        return request("cmd/mode", "{\"mode\":\"" + mode + "\"}", err);
    }

    std::string navigateTo(const Pose2D &goal, std::string *err = nullptr)
    {
        if (!finite(goal.x) || !finite(goal.y) || !finite(goal.theta)) {
            if (err)
                *err = "goal must contain finite numbers";
            return {};
        }
        return request("cmd/goto", "{\"x\":" + number(goal.x) + ",\"y\":"
                                       + number(goal.y) + ",\"theta\":"
                                       + number(goal.theta) + "}", err);
    }

    std::string cancelNavigation(std::string *err = nullptr)
    {
        return request("cmd/nav_cancel", "{}", err);
    }

    std::string startMission(std::string *err = nullptr)
    {
        return request("cmd/mission/start", "{}", err);
    }
    std::string pauseMission(std::string *err = nullptr)
    {
        return request("cmd/mission/pause", "{}", err);
    }
    std::string resumeMission(std::string *err = nullptr)
    {
        return request("cmd/mission/resume", "{}", err);
    }
    std::string stopMission(std::string *err = nullptr)
    {
        return request("cmd/mission/stop", "{}", err);
    }

    std::string listMaps(std::string *err = nullptr)
    {
        return request("cmd/maps/list", "{}", err);
    }

    std::string selectMap(const std::string &mapId, std::string *err = nullptr)
    {
        if (mapId.empty()) {
            if (err)
                *err = "map id must not be empty";
            return {};
        }
        return request("cmd/maps/select", "{\"id\":\"" + escape(mapId) + "\"}", err);
    }

    std::string setPowerPolicy(int returnAt, int departAt, std::string *err = nullptr)
    {
        if (returnAt < 0 || returnAt > 100 || departAt < 0 || departAt > 100) {
            if (err)
                *err = "power policy values must be percentages from 0 to 100";
            return {};
        }
        return request("cmd/power/policy", "{\"return_at\":" + std::to_string(returnAt)
                                          + ",\"depart_at\":" + std::to_string(departAt) + "}", err);
    }

    std::string setWaypoints(const std::vector<Waypoint> &points, std::string *err = nullptr)
    {
        std::string json = "{\"points\":[";
        for (std::size_t i = 0; i < points.size(); ++i) {
            if (!finite(points[i].pose))
                return invalid(err, "waypoints must contain finite poses");
            if (i)
                json += ',';
            json += "{\"id\":\"" + escape(points[i].id) + "\"," + poseJson(points[i].pose) + '}';
        }
        return request("cmd/waypoints/set", json + "]}", err);
    }

    std::string setLocations(const std::vector<Location> &locations, std::string *err = nullptr)
    {
        std::string json = "{\"locations\":[";
        for (std::size_t i = 0; i < locations.size(); ++i) {
            if ((locations[i].kind != "dock" && locations[i].kind != "home")
                || !finite(locations[i].pose))
                return invalid(err, "locations require dock/home and finite poses");
            if (i)
                json += ',';
            json += "{\"kind\":\"" + locations[i].kind + "\"," + poseJson(locations[i].pose) + '}';
        }
        return request("cmd/locations/set", json + "]}", err);
    }

    std::string setMarkers(const std::vector<Marker> &markers, std::string *err = nullptr)
    {
        std::string json = "{\"markers\":[";
        for (std::size_t i = 0; i < markers.size(); ++i) {
            if (!finite(markers[i].pose))
                return invalid(err, "markers must contain finite poses");
            if (i)
                json += ',';
            json += "{\"id\":" + std::to_string(markers[i].id) + ',' + poseJson(markers[i].pose) + '}';
        }
        return request("cmd/markers/set", json + "]}", err);
    }

    std::string triggerCapture(const std::string &vehicleNumber = "UNKNOWN",
                               const std::string &carNumber = "00",
                               const std::string &pointId = "MANUAL",
                               std::optional<int> tagId = std::nullopt,
                               std::string *err = nullptr)
    {
        std::string json = "{\"vehicle_number\":\"" + escape(vehicleNumber)
                           + "\",\"car_number\":\"" + escape(carNumber)
                           + "\",\"point_id\":\"" + escape(pointId) + "\"";
        if (tagId)
            json += ",\"tag_id\":" + std::to_string(*tagId);
        return request("cmd/capture/trigger", json + '}', err);
    }

    /// Commissioning-only arm APIs. Do not expose them in a customer operator
    /// UI until the delivered FR3 authority path has been approved.
    std::string armPreset(const std::string &name, std::string *err = nullptr)
    {
        if (name.empty()) {
            if (err)
                *err = "arm preset must not be empty";
            return {};
        }
        return request("cmd/arm/preset", "{\"name\":\"" + escape(name) + "\"}", err);
    }

    std::string armJointGoal(const std::vector<double> &positions, std::string *err = nullptr)
    {
        if (positions.empty()) {
            if (err)
                *err = "arm joint goal must not be empty";
            return {};
        }
        std::string values = "[";
        for (std::size_t i = 0; i < positions.size(); ++i) {
            if (!finite(positions[i])) {
                if (err)
                    *err = "arm joint goal must contain finite numbers";
                return {};
            }
            if (i)
                values += ',';
            values += number(positions[i]);
        }
        values += ']';
        return request("cmd/arm/joint_goal", "{\"positions\":" + values + "}", err);
    }

    std::string armStop(std::string *err = nullptr)
    {
        return request("cmd/arm/stop", "{}", err);
    }

    /// The only command without a response.  Call at 20 Hz while manual jog is
    /// intended; the robot stops it after 300 ms without a command.
    bool publishVelocity(const Twist2D &velocity, std::string *err = nullptr)
    {
        if (!finite(velocity.vx) || !finite(velocity.vy) || !finite(velocity.wz)) {
            if (err)
                *err = "velocity must contain finite numbers";
            return false;
        }
        return client_.publish("cmd/cmd_vel", "{\"vx\":" + number(velocity.vx)
                                              + ",\"vy\":" + number(velocity.vy)
                                              + ",\"wz\":" + number(velocity.wz) + "}", err);
    }

    /// Advanced configuration and commissioning channels use this method.
    /// The payload must be a JSON object as specified in command.md.
    std::string request(const std::string &channel, const std::string &payloadJson = "{}",
                        std::string *err = nullptr)
    {
        return client_.sendRequest(channel, payloadJson, err);
    }

private:
    static bool finite(double value) { return std::isfinite(value); }
    static bool finite(const Pose2D &pose)
    {
        return finite(pose.x) && finite(pose.y) && finite(pose.theta);
    }

    static std::string invalid(std::string *err, const char *message)
    {
        if (err)
            *err = message;
        return {};
    }

    static std::string number(double value)
    {
        char out[64];
        std::snprintf(out, sizeof out, "%.12g", value);
        return out;
    }

    static std::string poseJson(const Pose2D &pose)
    {
        return "\"x\":" + number(pose.x) + ",\"y\":" + number(pose.y)
               + ",\"theta\":" + number(pose.theta);
    }

    static std::string escape(const std::string &value)
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

    Client &client_;
};

}  // namespace shalom
