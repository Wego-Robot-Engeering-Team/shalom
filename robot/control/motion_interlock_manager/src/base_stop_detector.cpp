// Copyright (c) 2026 WeGo Robotics. All rights reserved.

#include "motion_interlock_manager/base_stop_detector.hpp"

#include <cmath>
#include <utility>

namespace motion_interlock_manager {

BaseStopDetector::BaseStopDetector(Config config) : config_(std::move(config)) {}

void BaseStopDetector::observe_command(bool zero, TimePoint now) {
  command_zero_ = zero;
  last_command_ = now;
  if (!zero) candidate_since_.reset();
}

void BaseStopDetector::observe_odometry(
    double linear_speed, double angular_speed, TimePoint now) {
  linear_speed_ = std::abs(linear_speed);
  angular_speed_ = std::abs(angular_speed);
  last_odometry_ = now;
  if (linear_speed_ > config_.linear_threshold ||
      angular_speed_ > config_.angular_threshold) {
    candidate_since_.reset();
  }
}

bool BaseStopDetector::stopped(TimePoint now) {
  if (!stop_conditions_hold(now)) {
    candidate_since_.reset();
    return false;
  }
  if (!candidate_since_) {
    candidate_since_ = now;
    return config_.settle_time.count() == 0;
  }
  return now - *candidate_since_ >= config_.settle_time;
}

bool BaseStopDetector::stop_conditions_hold(TimePoint now) const {
  if (!last_command_ || !last_odometry_ || !command_zero_) return false;
  if (now - *last_command_ > config_.command_timeout ||
      now - *last_odometry_ > config_.odometry_timeout) {
    return false;
  }
  return linear_speed_ <= config_.linear_threshold &&
         angular_speed_ <= config_.angular_threshold;
}

}  // namespace motion_interlock_manager
