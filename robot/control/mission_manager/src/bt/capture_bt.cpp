// Copyright (c) 2026 WeGo Robotics. All rights reserved.

#include "mission_manager/bt/capture_bt.hpp"

namespace mission_manager::bt {

Status CaptureBt::tick(CaptureRuntime & runtime) {
  while (true) {
    Status status = Status::kFailure;
    switch (next_step_) {
      case Step::kCorrectPose:
        status = runtime.correct_pose_with_apriltag();
        if (status == Status::kSuccess) next_step_ = Step::kPositionArm;
        break;
      case Step::kPositionArm:
        status = runtime.move_arm_to_capture_pose();
        if (status == Status::kSuccess) next_step_ = Step::kConfirmStop;
        break;
      case Step::kConfirmStop:
        status = runtime.confirm_robot_stopped();
        if (status == Status::kSuccess) next_step_ = Step::kCapture;
        break;
      case Step::kCapture:
        status = runtime.capture_and_store();
        if (status == Status::kSuccess) next_step_ = Step::kCorrectPose;
        break;
    }
    if (status != Status::kSuccess || next_step_ == Step::kCorrectPose) return status;
  }
}

void CaptureBt::halt(CaptureRuntime & runtime) {
  runtime.halt_capture();
  next_step_ = Step::kCorrectPose;
}

}  // namespace mission_manager::bt
