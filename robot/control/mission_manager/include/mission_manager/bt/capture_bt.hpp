#pragma once

#include "mission_manager/bt/status.hpp"

namespace mission_manager::bt {

class CaptureRuntime {
public:
  virtual ~CaptureRuntime() = default;
  virtual Status correct_pose_with_apriltag() = 0;
  virtual Status move_arm_to_capture_pose() = 0;
  virtual Status confirm_robot_stopped() = 0;
  virtual Status capture_and_store() = 0;
  virtual void halt_capture() = 0;
};

// AprilTag correction -> arm positioning -> base stop confirmation -> capture.
class CaptureBt {
public:
  Status tick(CaptureRuntime & runtime);
  void halt(CaptureRuntime & runtime);

private:
  enum class Step { kCorrectPose, kPositionArm, kConfirmStop, kCapture };
  Step next_step_{Step::kCorrectPose};
};

}  // namespace mission_manager::bt
