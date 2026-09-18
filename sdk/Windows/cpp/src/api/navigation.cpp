// Copyright (c) 2026 WeGo Robotics. All rights reserved.

#include "api/detail.hpp"
#include "shalom/api.hpp"

namespace shalom {

std::string RobotApi::navigateTo(const Pose2D &goal, std::string *err)
{
    if (!api::detail::finite(goal))
        return api::detail::invalid(err, "goal must contain finite numbers");
    return client_.sendRequest("cmd/goto", "{\"x\":" + api::detail::number(goal.x)
                                         + ",\"y\":" + api::detail::number(goal.y)
                                         + ",\"theta\":" + api::detail::number(goal.theta) + "}", err);
}

std::string RobotApi::cancelNavigation(std::string *err)
{
    return client_.sendRequest("cmd/nav_cancel", "{}", err);
}

bool RobotApi::publishVelocity(const Twist2D &velocity, std::string *err)
{
    (void)velocity;
    if (err)
        *err = "manual velocity is available only through the commissioned HMI UDP teleop path";
    return false;
}

}  // namespace shalom
