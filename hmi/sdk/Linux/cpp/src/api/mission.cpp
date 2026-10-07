// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include "robot_sdk/api.hpp"
#include "api/detail.hpp"

namespace robot_sdk {

std::string RobotApi::startMission(const std::string &id, std::string *err) {
    if (!api::detail::validId(id)) return api::detail::invalid(err, "invalid mission id");
    return client_.sendRequest("cmd/mission/start", nlohmann::json{{"mission_id",id}}.dump(), err, 5000);
}
std::string RobotApi::pauseMission(std::string *err) { return client_.sendRequest("cmd/mission/pause", "{}", err); }
std::string RobotApi::resumeMission(std::string *err) { return client_.sendRequest("cmd/mission/resume", "{}", err); }
std::string RobotApi::stopMission(std::string *err) { return client_.sendRequest("cmd/mission/stop", "{}", err); }

std::string RobotApi::returnToDock(std::string *err) { return client_.sendRequest("cmd/mission/return_dock", "{}", err, 5000); }
std::string RobotApi::listMissions(std::string *err) { return client_.sendRequest("cmd/missions/list", "{}", err); }
std::string RobotApi::saveMission(const std::string &mission, const std::string &map, std::uint64_t revision, std::string *err) {
    if (!api::detail::validText(map)) return api::detail::invalid(err, "invalid edit map id");
    return api::detail::objectRequest(client_, "cmd/missions/save", mission, "mission", revision, map, err);
}
std::string RobotApi::archiveMission(const std::string &id, const std::string &map, std::uint64_t revision, std::string *err) {
    if (!api::detail::validId(id) || !api::detail::validText(map) || !revision) return api::detail::invalid(err, "invalid mission id/map/revision");
    return client_.sendRequest("cmd/missions/archive", nlohmann::json{{"id",id},{"map_id",map},{"expected_revision",revision}}.dump(), err);
}

}  // namespace robot_sdk
