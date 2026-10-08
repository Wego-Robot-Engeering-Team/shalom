// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include "hmi_bridge/bridge_node.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <lifecycle_msgs/msg/state.hpp>

namespace hmi_bridge {

json BridgeNode::navigationSpeedSettings() const
{
    return json{{"speed_limit_mps", navigationSpeed_},
                {"min_speed_mps", navigationMinSpeed_}, {"max_speed_mps", navigationMaxSpeed_},
                {"angular_speed_limit_rps", navigationAngularSpeed_},
                {"min_angular_speed_rps", navigationMinAngularSpeed_},
                {"max_angular_speed_rps", navigationMaxAngularSpeed_}};
}

bool BridgeNode::readNavigationSpeed(const json &input, bool requireAll,
                                      json *settings, std::string *error) const
{
    const bool rangesOnly = requireAll && input.is_object() &&
        !input.contains("speed_limit_mps") && !input.contains("angular_speed_limit_rps");
    if (!input.is_object() || (!rangesOnly && !input.contains("speed_limit_mps"))) {
        *error = "선속도 설정값이 필요합니다";
        return false;
    }
    *settings = navigationSpeedSettings();
    for (auto &[key, value] : settings->items()) {
        const auto supplied = input.find(key);
        if (supplied == input.end()) {
            if (!requireAll) continue; // Legacy files and linear-only commands.
            if (rangesOnly && (key == "speed_limit_mps" || key == "angular_speed_limit_rps"))
                continue;
            *error = "속도 설정 필드가 누락되었습니다: " + key;
            return false;
        }
        if (!supplied->is_number() || !std::isfinite(supplied->get<double>())) {
            *error = "속도 설정은 유한한 숫자여야 합니다: " + key;
            return false;
        }
        value = *supplied;
    }
    const double minimum = settings->at("min_speed_mps").get<double>();
    const double maximum = settings->at("max_speed_mps").get<double>();
    const double angularMinimum = settings->at("min_angular_speed_rps").get<double>();
    const double angularMaximum = settings->at("max_angular_speed_rps").get<double>();
    if (minimum > maximum || angularMinimum > angularMaximum) {
        *error = "최소값은 최대값 이하로 입력하십시오";
        return false;
    }
    if (rangesOnly) {
        // Preserve the robot's current values, not the HMI's last snapshot.
        (*settings)["speed_limit_mps"] = std::clamp(navigationSpeed_, minimum, maximum);
        (*settings)["angular_speed_limit_rps"] = std::clamp(navigationAngularSpeed_, angularMinimum, angularMaximum);
    }
    const double linear = settings->at("speed_limit_mps").get<double>();
    const double angular = settings->at("angular_speed_limit_rps").get<double>();
    if (minimum < 0.10 || maximum > 0.60 || minimum > linear || linear > maximum) {
        *error = "선속도는 0.10 ≤ 최소 ≤ 설정값 ≤ 최대 ≤ 0.60 m/s여야 합니다";
        return false;
    }
    if (angularMinimum < 0.05 || angularMaximum > 0.80 ||
        angularMinimum > angular || angular > angularMaximum) {
        *error = "각속도는 0.05 ≤ 최소 ≤ 설정값 ≤ 최대 ≤ 0.80 rad/s여야 합니다";
        return false;
    }
    return true;
}

// Keep MPPI's base limits equal to the robot setting. Its absolute SpeedLimit
// then has a ratio of one, so linear changes no longer scale the angular limit.
// Backward/lateral limits retain the platform's 0.4/0.6 linear ratio.
void BridgeNode::syncNavigationSpeed()
{
    using namespace std::chrono_literals;
    const auto stamp = std::chrono::steady_clock::now();
    if (navigationSpeedSyncPending_) {
        if (stamp - navigationSpeedSyncAt_ < 2s) return;
        if (navigationSpeedStateId_)
            navigationSpeedStateClient_->remove_pending_request(*navigationSpeedStateId_);
        if (navigationSpeedGetId_)
            navigationSpeedGetClient_->remove_pending_request(*navigationSpeedGetId_);
        if (navigationSpeedSetId_)
            navigationSpeedSetClient_->remove_pending_request(*navigationSpeedSetId_);
        navigationSpeedGetId_.reset();
        navigationSpeedStateId_.reset();
        navigationSpeedSetId_.reset();
        navigationSpeedSyncPending_ = false;
        navigationSpeedApplied_ = false;
        navigationControllerActive_ = false;
        ++navigationSpeedGeneration_;
    }
    if (stamp - navigationSpeedSyncAt_ < 1s) return;
    navigationSpeedSyncAt_ = stamp;
    if (!navigationSpeedStateClient_->service_is_ready() ||
        !navigationSpeedGetClient_->service_is_ready() ||
        !navigationSpeedSetClient_->service_is_ready()) {
        navigationControllerActive_ = false;
        if (navigationSpeedApplied_) {
            navigationSpeedApplied_ = false;
            publishNavigationSpeed();
        }
        return;
    }
    const double linear = navigationSpeed_, angular = navigationAngularSpeed_;
    const auto generation = ++navigationSpeedGeneration_;
    navigationSpeedSyncPending_ = true;
    auto request = std::make_shared<lifecycle_msgs::srv::GetState::Request>();
    navigationSpeedStateId_ = navigationSpeedStateClient_->async_send_request(request,
        [this, generation, linear, angular](rclcpp::Client<lifecycle_msgs::srv::GetState>::SharedFuture future) {
            if (generation != navigationSpeedGeneration_) return;
            navigationSpeedStateId_.reset();
            try {
                const bool active = future.get()->current_state.id == lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE;
                if (!active) {
                    navigationControllerActive_ = false;
                    navigationSpeedSyncPending_ = false;
                    if (navigationSpeedApplied_) {
                        navigationSpeedApplied_ = false;
                        publishNavigationSpeed();
                    }
                    return;
                }
                const bool force = !navigationControllerActive_;
                navigationControllerActive_ = true;
                queryControllerSpeed(generation, linear, angular, force);
            } catch (const std::exception &e) {
                navigationControllerActive_ = false;
                navigationSpeedSyncPending_ = false;
                navigationSpeedApplied_ = false;
                RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000,
                    "Nav2 활성 상태 확인 실패: %s", e.what());
            }
        }).request_id;
}

void BridgeNode::queryControllerSpeed(uint64_t generation, double linear, double angular, bool force)
{
    auto request = std::make_shared<rcl_interfaces::srv::GetParameters::Request>();
    for (const auto *name : {"vx_max", "vx_min", "vy_max", "wz_max"})
        request->names.push_back(navigationControllerId_ + "." + name);
    navigationSpeedGetId_ = navigationSpeedGetClient_->async_send_request(request,
        [this, generation, linear, angular, force](rclcpp::Client<rcl_interfaces::srv::GetParameters>::SharedFuture future) {
            if (generation != navigationSpeedGeneration_) return;
            navigationSpeedGetId_.reset();
            try {
                const auto values = future.get()->values;
                const std::array<double, 4> expected{linear, -linear * (2.0 / 3.0),
                                                    linear * (2.0 / 3.0), angular};
                bool matches = values.size() == expected.size();
                for (size_t i = 0; matches && i < expected.size(); ++i)
                    matches = values[i].type == rcl_interfaces::msg::ParameterType::PARAMETER_DOUBLE &&
                              std::isfinite(values[i].double_value) &&
                              std::abs(values[i].double_value - expected[i]) < 1e-8;
                if (!matches || force) {
                    navigationSpeedApplied_ = false;
                    setControllerSpeed(generation, linear, angular);
                    return;
                }
                navigationSpeedSyncPending_ = false;
                const bool wasApplied = navigationSpeedApplied_;
                navigationSpeedApplied_ = true;
                publishSpeedLimit();
                if (!wasApplied) publishNavigationSpeed();
            } catch (const std::exception &e) {
                navigationSpeedSyncPending_ = false;
                navigationSpeedApplied_ = false;
                RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000,
                    "Nav2 속도 설정 확인 실패: %s", e.what());
            }
        }).request_id;
}

void BridgeNode::setControllerSpeed(uint64_t generation, double linear, double angular)
{
    auto request = std::make_shared<rcl_interfaces::srv::SetParametersAtomically::Request>();
    const std::array<std::pair<const char *, double>, 4> settings{{
        {"vx_max", linear}, {"vx_min", -linear * (2.0 / 3.0)},
        {"vy_max", linear * (2.0 / 3.0)}, {"wz_max", angular}}};
    for (const auto &[name, value] : settings)
        request->parameters.push_back(rclcpp::Parameter(navigationControllerId_ + "." + name, value).to_parameter_msg());
    navigationSpeedSetId_ = navigationSpeedSetClient_->async_send_request(request,
        [this, generation](rclcpp::Client<rcl_interfaces::srv::SetParametersAtomically>::SharedFuture future) {
            if (generation != navigationSpeedGeneration_) return;
            navigationSpeedSetId_.reset();
            navigationSpeedSyncPending_ = false;
            try {
                const auto result = future.get()->result;
                navigationSpeedApplied_ = result.successful;
                if (!result.successful)
                    RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000,
                        "Nav2 속도 설정 적용 실패: %s", result.reason.c_str());
            } catch (const std::exception &e) {
                navigationSpeedApplied_ = false;
                RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000,
                    "Nav2 속도 설정 적용 실패: %s", e.what());
            }
            publishSpeedLimit();
            publishNavigationSpeed();
        }).request_id;
}

}  // namespace hmi_bridge
