// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary
#include "hmi_bridge/bridge_node.hpp"

namespace hmi_bridge {
using namespace std::chrono_literals;

void BridgeNode::pollNavigationReadiness()
{
    const auto current = std::chrono::steady_clock::now();
    for (size_t i = 0; i < navigationLifecycle_.size(); ++i) {
        auto &observation = navigationLifecycle_[i];
        if (current - observation.received > 3s)
            observation.state = "unknown";
        if (observation.pending) {
            if (current - observation.requested < 2s) continue;
            observation.client->remove_pending_request(*observation.pending);
            observation.pending.reset();
            ++observation.generation;
            observation.state = "unknown";
        }
        if (!observation.client->service_is_ready()) {
            observation.state = "unknown";
            continue;
        }
        const auto generation = ++observation.generation;
        observation.requested = current;
        const auto future = observation.client->async_send_request(
            std::make_shared<lifecycle_msgs::srv::GetState::Request>(),
            [this, i, generation](rclcpp::Client<lifecycle_msgs::srv::GetState>::SharedFuture response) {
                auto &entry = navigationLifecycle_[i];
                if (generation != entry.generation) return;
                entry.pending.reset();
                try {
                    switch (response.get()->current_state.id) {
                    case 1: entry.state = "unconfigured"; break;
                    case 2: entry.state = "inactive"; break;
                    case 3: entry.state = "active"; break;
                    case 4: entry.state = "finalized"; break;
                    default: entry.state = "unknown"; break;
                    }
                    entry.received = std::chrono::steady_clock::now();
                } catch (const std::exception &) {
                    entry.state = "unknown";
                }
            });
        observation.pending = future.request_id;
    }
}

std::string BridgeNode::navigationReadiness() const
{
    if (navigationLifecycle_.size() != 5) return "unknown";
    bool unknown = false;
    for (size_t i = 0; i < 3; ++i) {
        const auto &state = navigationLifecycle_[i].state;
        if (state == "unknown") unknown = true;
        else if (state != "active") return state;
    }
    return unknown ? "unknown" : "active";
}

std::string BridgeNode::localizationReadiness() const
{
    if (navigationLifecycle_.size() != 5) return "unknown";
    // Live mapping uses SLAM; a saved map uses AMCL. "active" reports the
    // lifecycle state, not convergence of the pose estimate.
    return navigationLifecycle_[mapId_ == "live" ? 4 : 3].state;
}
}  // namespace hmi_bridge
