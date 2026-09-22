// Copyright (c) 2026 WeGo Robotics. All rights reserved.

#include <chrono>
#include <cstdlib>
#include <iostream>

#include "motion_interlock_manager/base_stop_detector.hpp"

namespace {

using namespace std::chrono_literals;
using Detector = motion_interlock_manager::BaseStopDetector;

void expect(bool condition, const char * message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
  }
}

Detector make_detector() {
  Detector::Config config;
  config.linear_threshold = 0.03;
  config.angular_threshold = 0.05;
  config.command_timeout = 250ms;
  config.odometry_timeout = 500ms;
  config.settle_time = 200ms;
  return Detector(config);
}

void test_requires_fresh_command_and_odometry() {
  auto detector = make_detector();
  const Detector::TimePoint start{};
  expect(!detector.stopped(start), "startup without feedback must not report stopped");
  detector.observe_command(true, start);
  expect(!detector.stopped(start), "zero command alone must not report stopped");
  detector.observe_odometry(0.0, 0.0, start);
  expect(!detector.stopped(start), "settling interval must be observed");
  expect(detector.stopped(start + 200ms), "fresh stable zero feedback must report stopped");
}

void test_motion_or_nonzero_command_resets_settling() {
  auto detector = make_detector();
  const Detector::TimePoint start{};
  detector.observe_command(true, start);
  detector.observe_odometry(0.0, 0.0, start);
  detector.stopped(start);
  detector.observe_odometry(0.2, 0.0, start + 100ms);
  expect(!detector.stopped(start + 100ms), "measured motion must clear stopped state");
  detector.observe_odometry(0.0, 0.0, start + 150ms);
  detector.observe_command(true, start + 150ms);
  expect(!detector.stopped(start + 150ms), "settling must restart after motion");
  expect(!detector.stopped(start + 300ms), "settling must restart after motion");
  expect(detector.stopped(start + 350ms), "stopped may assert after the restarted interval");

  detector.observe_command(false, start + 360ms);
  expect(!detector.stopped(start + 360ms), "nonzero command must clear stopped state");
}

void test_stale_feedback_fails_closed() {
  auto detector = make_detector();
  const Detector::TimePoint start{};
  detector.observe_command(true, start);
  detector.observe_odometry(0.0, 0.0, start);
  detector.stopped(start);
  expect(!detector.stopped(start + 501ms), "stale feedback must never report stopped");
}

}  // namespace

int main() {
  test_requires_fresh_command_and_odometry();
  test_motion_or_nonzero_command_resets_settling();
  test_stale_feedback_fails_closed();
  return 0;
}
