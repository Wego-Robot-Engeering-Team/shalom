// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "mission_manager/bt/capture_bt.hpp"
#include "mission_manager/bt/nav2_bt.hpp"
#include "mission_manager/bt/stair_bt.hpp"
#include "mission_manager/operation_registry.hpp"

namespace {

using mission_manager::bt::CaptureBt;
using mission_manager::bt::CaptureRuntime;
using mission_manager::bt::Nav2Bt;
using mission_manager::bt::Nav2Runtime;
using mission_manager::bt::StairBt;
using mission_manager::bt::StairRuntime;
using mission_manager::bt::Status;
using mission_manager::execution::OperationRegistry;

void expect(bool condition, const char * message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
  }
}

class FakeRuntime final : public Nav2Runtime, public CaptureRuntime, public StairRuntime {
public:
  Status navigate_to(const std::string &) override { return next(navigation_); }
  void cancel_navigation() override { ++cancelled_navigation_; }
  Status correct_pose_with_apriltag() override { return next(tag_); }
  Status move_arm_to_capture_pose() override { return next(arm_); }
  Status confirm_robot_stopped() override { return next(stopped_); }
  Status capture_and_store() override { return next(capture_); }
  void halt_capture() override { ++halted_capture_; }
  Status confirm_stair_route_safe() override { return next(stair_safety_); }
  Status engage_stair_mode() override { return next(stair_mode_); }
  Status traverse_stairs() override { return next(traverse_); }
  Status confirm_stair_exit() override { return next(stair_exit_); }
  void halt_stair_motion() override { ++halted_stairs_; }

  std::vector<Status> navigation_{Status::Success};
  std::vector<Status> tag_{Status::Success};
  std::vector<Status> arm_{Status::Success};
  std::vector<Status> stopped_{Status::Success};
  std::vector<Status> capture_{Status::Success};
  std::vector<Status> stair_safety_{Status::Success};
  std::vector<Status> stair_mode_{Status::Success};
  std::vector<Status> traverse_{Status::Success};
  std::vector<Status> stair_exit_{Status::Success};
  int cancelled_navigation_{0};
  int halted_capture_{0};
  int halted_stairs_{0};

private:
  static Status next(std::vector<Status> & statuses) {
    const Status value = statuses.front();
    if (statuses.size() > 1) statuses.erase(statuses.begin());
    return value;
  }
};

void test_nav2_bt_tracks_and_cancels_one_goal() {
  FakeRuntime runtime;
  runtime.navigation_ = {Status::Running, Status::Success};
  Nav2Bt nav2;
  expect(nav2.tick(runtime, "inspection-01") == Status::Running,
         "Nav2 goal must tick asynchronously");
  expect(nav2.tick(runtime, "inspection-02") == Status::Failure,
         "active goal must not be silently replaced");
  nav2.halt(runtime);
  expect(runtime.cancelled_navigation_ == 1, "halt must cancel active Nav2 goal");
}

void test_capture_bt_requires_all_steps() {
  FakeRuntime runtime;
  runtime.tag_ = {Status::Running, Status::Success};
  CaptureBt capture;
  expect(capture.tick(runtime) == Status::Running,
         "tag correction must complete before capture");
  expect(capture.tick(runtime) == Status::Success,
         "capture BT must finish all capture steps");
}

void test_stair_bt_fails_closed() {
  FakeRuntime runtime;
  runtime.stair_safety_ = {Status::Failure};
  StairBt stairs;
  expect(stairs.tick(runtime) == Status::Failure,
         "unsafe stair route must fail before stair motion");
  stairs.halt(runtime);
  expect(runtime.halted_stairs_ == 1, "stair halt must reach mobility adapter");
}

void test_operation_registry_requires_an_implemented_executor() {
  OperationRegistry registry;
  int ticks = 0;
  int halts = 0;
  expect(registry.add(
      0, {"navigate", {"navigation"},
        [&ticks](const std::string &) {
          ++ticks;
          return OperationRegistry::Result{Status::Success};
        },
        [&halts]() { ++halts; }}),
    "implemented operation must be registered");
  expect(!registry.add(
      0, {"duplicate", {},
        [](const std::string &) { return OperationRegistry::Result{Status::Success}; },
        []() {}}),
    "duplicate operation IDs must be rejected");
  expect(registry.find(1) == nullptr, "unimplemented operation must remain unavailable");

  const auto * executor = registry.find(0);
  expect(executor != nullptr, "registered operation must be discoverable");
  expect(executor->required_capabilities.size() == 1 &&
         executor->required_capabilities.front() == "navigation",
    "executor must declare its required capabilities");
  expect(executor->tick("waypoint-1").status == Status::Success && ticks == 1,
    "registry must invoke the registered tick callback");
  executor->halt();
  expect(halts == 1, "registry must invoke the registered halt callback");
}

}  // namespace

int main() {
  test_nav2_bt_tracks_and_cancels_one_goal();
  test_capture_bt_requires_all_steps();
  test_stair_bt_fails_closed();
  test_operation_registry_requires_an_implemented_executor();
  return 0;
}
