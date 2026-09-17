// Copyright (c) 2026 WeGo Robotics. All rights reserved.

#include <iostream>

#include "mission_manager/bt/capture_bt.hpp"
#include "mission_manager/bt/nav2_bt.hpp"
#include "mission_manager/bt/stair_bt.hpp"
#include "mission_manager/mission_fsm.hpp"

namespace {

class DemoRuntime final : public mission_manager::bt::Nav2Runtime,
                          public mission_manager::bt::CaptureRuntime,
                          public mission_manager::bt::StairRuntime {
public:
  mission_manager::bt::Status navigate_to(const std::string &) override {
    return nav_started_ ? mission_manager::bt::Status::kSuccess
                        : (nav_started_ = true, mission_manager::bt::Status::kRunning);
  }
  void cancel_navigation() override {}
  mission_manager::bt::Status correct_pose_with_apriltag() override { return mission_manager::bt::Status::kSuccess; }
  mission_manager::bt::Status move_arm_to_capture_pose() override { return mission_manager::bt::Status::kSuccess; }
  mission_manager::bt::Status confirm_robot_stopped() override { return mission_manager::bt::Status::kSuccess; }
  mission_manager::bt::Status capture_and_store() override { return mission_manager::bt::Status::kSuccess; }
  void halt_capture() override {}
  mission_manager::bt::Status confirm_stair_route_safe() override { return mission_manager::bt::Status::kSuccess; }
  mission_manager::bt::Status engage_stair_mode() override { return mission_manager::bt::Status::kSuccess; }
  mission_manager::bt::Status traverse_stairs() override { return mission_manager::bt::Status::kSuccess; }
  mission_manager::bt::Status confirm_stair_exit() override { return mission_manager::bt::Status::kSuccess; }
  void halt_stair_motion() override {}

private:
  bool nav_started_{false};
};

void print(const char * name, mission_manager::bt::Status status) {
  std::cout << name << '=' << mission_manager::bt::to_string(status) << '\n';
}

}  // namespace

int main() {
  using mission_manager::MissionEvent;
  using mission_manager::MissionFsm;

  MissionFsm fsm;
  DemoRuntime runtime;
  mission_manager::bt::Nav2Bt nav2;
  mission_manager::bt::CaptureBt capture;
  mission_manager::bt::StairBt stairs;
  fsm.dispatch(MissionEvent::kStart);

  print("nav2", nav2.tick(runtime, "inspection-01"));
  print("nav2", nav2.tick(runtime, "inspection-01"));
  print("capture", capture.tick(runtime));
  print("stairs", stairs.tick(runtime));
  fsm.dispatch(MissionEvent::kMissionComplete);
  print("dock", nav2.tick(runtime, "dock"));
  fsm.dispatch(MissionEvent::kReturnComplete);
  std::cout << "state=" << mission_manager::to_string(fsm.state()) << '\n';
  return 0;
}
