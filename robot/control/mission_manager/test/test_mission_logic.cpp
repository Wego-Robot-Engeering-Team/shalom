// Copyright (c) 2026 WeGo Robotics. All rights reserved.

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "mission_manager/bt/capture_bt.hpp"
#include "mission_manager/bt/nav2_bt.hpp"
#include "mission_manager/bt/stair_bt.hpp"
#include "mission_manager/mission_fsm.hpp"

namespace {

using mission_manager::MissionEvent;
using mission_manager::MissionFsm;
using mission_manager::MissionState;
using mission_manager::bt::CaptureBt;
using mission_manager::bt::CaptureRuntime;
using mission_manager::bt::Nav2Bt;
using mission_manager::bt::Nav2Runtime;
using mission_manager::bt::StairBt;
using mission_manager::bt::StairRuntime;
using mission_manager::bt::Status;

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

  std::vector<Status> navigation_{Status::kSuccess};
  std::vector<Status> tag_{Status::kSuccess};
  std::vector<Status> arm_{Status::kSuccess};
  std::vector<Status> stopped_{Status::kSuccess};
  std::vector<Status> capture_{Status::kSuccess};
  std::vector<Status> stair_safety_{Status::kSuccess};
  std::vector<Status> stair_mode_{Status::kSuccess};
  std::vector<Status> traverse_{Status::kSuccess};
  std::vector<Status> stair_exit_{Status::kSuccess};
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

void test_safety_transitions_require_explicit_resume() {
  MissionFsm fsm;
  expect(fsm.dispatch(MissionEvent::kStart).accepted, "start must be accepted");
  fsm.dispatch(MissionEvent::kLinkLost);
  expect(fsm.state() == MissionState::kPaused, "link loss must pause the mission");
  fsm.dispatch(MissionEvent::kEmergencyStop);
  fsm.dispatch(MissionEvent::kEmergencyStopReleased);
  expect(fsm.state() == MissionState::kPaused, "E-stop release must not restart motion");
  fsm.dispatch(MissionEvent::kResume);
  expect(fsm.state() == MissionState::kRunning, "operator resume must restart mission");
}

void test_nav2_bt_tracks_and_cancels_one_goal() {
  FakeRuntime runtime;
  runtime.navigation_ = {Status::kRunning, Status::kSuccess};
  Nav2Bt nav2;
  expect(nav2.tick(runtime, "inspection-01") == Status::kRunning, "Nav2 goal must tick asynchronously");
  expect(nav2.tick(runtime, "inspection-02") == Status::kFailure, "active goal must not be silently replaced");
  nav2.halt(runtime);
  expect(runtime.cancelled_navigation_ == 1, "halt must cancel active Nav2 goal");
}

void test_capture_bt_requires_all_steps() {
  FakeRuntime runtime;
  runtime.tag_ = {Status::kRunning, Status::kSuccess};
  CaptureBt capture;
  expect(capture.tick(runtime) == Status::kRunning, "tag correction must complete before capture");
  expect(capture.tick(runtime) == Status::kSuccess, "capture BT must finish all capture steps");
}

void test_stair_bt_fails_closed() {
  FakeRuntime runtime;
  runtime.stair_safety_ = {Status::kFailure};
  StairBt stairs;
  expect(stairs.tick(runtime) == Status::kFailure, "unsafe stair route must fail before stair motion");
  stairs.halt(runtime);
  expect(runtime.halted_stairs_ == 1, "stair halt must reach mobility adapter");
}

}  // namespace

int main() {
  test_safety_transitions_require_explicit_resume();
  test_nav2_bt_tracks_and_cancels_one_goal();
  test_capture_bt_requires_all_steps();
  test_stair_bt_fails_closed();
  return 0;
}
