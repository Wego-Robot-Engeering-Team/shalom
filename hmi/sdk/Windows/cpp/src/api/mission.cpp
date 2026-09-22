// Copyright (c) 2026 WeGo Robotics. All rights reserved.

#include "shalom/api.hpp"

namespace shalom {

std::string RobotApi::startMission(std::string *err) { return client_.sendRequest("cmd/mission/start", "{}", err); }
std::string RobotApi::pauseMission(std::string *err) { return client_.sendRequest("cmd/mission/pause", "{}", err); }
std::string RobotApi::resumeMission(std::string *err) { return client_.sendRequest("cmd/mission/resume", "{}", err); }
std::string RobotApi::stopMission(std::string *err) { return client_.sendRequest("cmd/mission/stop", "{}", err); }

}  // namespace shalom
