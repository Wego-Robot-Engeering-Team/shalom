// Copyright (c) 2026 WeGo Robotics. All rights reserved.

#pragma once

#include "shalom/api/detail.hpp"
#include "shalom/client.hpp"

#include <string>

namespace shalom::api {

/// Goal navigation and manual base-velocity commands.
class NavigationApi {
public:
    explicit NavigationApi(Client &client) : client_(client) {}

    std::string navigateTo(const Pose2D &goal, std::string *err = nullptr)
    {
        if (!detail::finite(goal))
            return detail::invalid(err, "goal must contain finite numbers");
        return client_.sendRequest("cmd/goto", "{\"x\":" + detail::number(goal.x)
                                                + ",\"y\":" + detail::number(goal.y)
                                                + ",\"theta\":" + detail::number(goal.theta) + "}", err);
    }

    std::string cancelNavigation(std::string *err = nullptr)
    {
        return client_.sendRequest("cmd/nav_cancel", "{}", err);
    }

    /// Publish at 20 Hz while manual jogging is intended; the robot stops it
    /// after 300 ms without a command.
    bool publishVelocity(const Twist2D &velocity, std::string *err = nullptr)
    {
        if (!detail::finite(velocity.vx) || !detail::finite(velocity.vy)
            || !detail::finite(velocity.wz)) {
            if (err)
                *err = "velocity must contain finite numbers";
            return false;
        }
        return client_.publish("cmd/cmd_vel", "{\"vx\":" + detail::number(velocity.vx)
                                              + ",\"vy\":" + detail::number(velocity.vy)
                                              + ",\"wz\":" + detail::number(velocity.wz) + "}", err);
    }

protected:
    Client &client_;
};

}  // namespace shalom::api
