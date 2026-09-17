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
    if (!api::detail::finite(velocity.vx) || !api::detail::finite(velocity.vy)
        || !api::detail::finite(velocity.wz)) {
        if (err)
            *err = "velocity must contain finite numbers";
        return false;
    }
    return client_.publish("cmd/cmd_vel", "{\"vx\":" + api::detail::number(velocity.vx)
                                      + ",\"vy\":" + api::detail::number(velocity.vy)
                                      + ",\"wz\":" + api::detail::number(velocity.wz) + "}", err);
}

}  // namespace shalom
