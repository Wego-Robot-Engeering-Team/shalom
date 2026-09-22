// Copyright (c) 2026 WeGo Robotics. All rights reserved.

#pragma once

#include <chrono>
#include <optional>

namespace motion_interlock_manager {

class BaseStopDetector {
public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;

  struct Config {
    double linear_threshold{0.03};
    double angular_threshold{0.05};
    std::chrono::milliseconds command_timeout{250};
    std::chrono::milliseconds odometry_timeout{500};
    std::chrono::milliseconds settle_time{200};
  };

  explicit BaseStopDetector(Config config);

  void observe_command(bool zero, TimePoint now);
  void observe_odometry(double linear_speed, double angular_speed, TimePoint now);

  // Returns true only after fresh zero command and fresh stopped feedback have
  // both remained valid for the configured settling interval.
  bool stopped(TimePoint now);

private:
  bool stop_conditions_hold(TimePoint now) const;

  Config config_;
  std::optional<TimePoint> last_command_;
  std::optional<TimePoint> last_odometry_;
  std::optional<TimePoint> candidate_since_;
  bool command_zero_{false};
  double linear_speed_{0.0};
  double angular_speed_{0.0};
};

}  // namespace motion_interlock_manager
