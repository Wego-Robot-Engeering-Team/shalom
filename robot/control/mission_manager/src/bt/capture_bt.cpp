// Copyright (c) 2026 WeGo Robotics. All rights reserved.

#include "mission_manager/bt/capture_bt.hpp"

namespace mission_manager::bt {

Status CaptureBt::tick(CaptureRuntime & runtime) {
  while (true) {
    Status status = Status::Failure;
    switch (next_step_) {
      case Step::CorrectPose:
        status = runtime.correct_pose_with_apriltag();
        if (status == Status::Success) next_step_ = Step::PositionArm;
        break;
      case Step::PositionArm:
        status = runtime.move_arm_to_capture_pose();
        if (status == Status::Success) next_step_ = Step::ConfirmStop;
        break;
      case Step::ConfirmStop:
        status = runtime.confirm_robot_stopped();
        if (status == Status::Success) next_step_ = Step::Capture;
        break;
      case Step::Capture:
        status = runtime.capture_and_store();
        if (status == Status::Success) next_step_ = Step::CorrectPose;
        break;
    }
    if (status != Status::Success || next_step_ == Step::CorrectPose) return status;
  }
}

void CaptureBt::halt(CaptureRuntime & runtime) {
  runtime.halt_capture();
  next_step_ = Step::CorrectPose;
}

}  // namespace mission_manager::bt
