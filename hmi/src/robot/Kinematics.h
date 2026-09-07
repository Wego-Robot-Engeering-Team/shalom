#pragma once

// Forward and inverse kinematics for the FR3, used only to keep the two ways
// of aiming the arm in step with each other.
//
// The joint tab and the end-effector tab describe the same target. Editing one
// and leaving the other stale means the operator is looking at a number that
// is not what will be sent. So joint edits update the pose, and pose edits
// update the joints.
//
// What this is NOT
// ----------------
// It does not decide how the arm moves. The station still sends the pose the
// operator asked for and MoveIt2 plans the path, checks collisions and picks
// its own joint solution - which may differ from the one here, because a
// six-axis arm reaches most poses in up to eight distinct configurations
// (elbow up or down, wrist flipped, shoulder left or right). Treating this
// solution as authoritative would put a second planner in the control station,
// disagreeing with the robot's, with no way for the operator to tell which is
// right.
//
// It is a preview. That is why inverse() takes the current joints as a seed:
// among those configurations it returns the one nearest where the arm already
// is, which is the one the operator is picturing.

#include <QList>
#include <QMatrix4x4>
#include <QVector3D>
#include <optional>

namespace hmi::robot {

/// End-effector pose in the arm base frame. Metres and radians.
struct EePose {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double roll = 0.0;
    double pitch = 0.0;
    double yaw = 0.0;
};

/// Pose of every joint frame, in the arm base frame.
///
/// `kArmJointCount + 1` transforms: the mounting flange, then one per joint.
/// The 3D view needs orientation as well as position to place a link mesh, so
/// this is the primitive and jointOrigins() is its translation column.
QList<QMatrix4x4> jointFrames(const QList<double> &joints);

/// Origin of every joint frame, in the arm base frame.
///
/// `kArmJointCount + 1` points: the base, then one per joint, the last of
/// which is the flange. Shared so the pose check, the 3D view and this file
/// cannot end up describing different arms - they each used to build their own
/// copy of the chain.
QList<QVector3D> jointOrigins(const QList<double> &joints);

/// Where the flange ends up for these six joint angles.
EePose forwardKinematics(const QList<double> &joints);

/// Joint angles that put the flange at `target`, starting the search from
/// `seed` and staying inside the FR3 travel limits.
///
/// Returns nullopt when the solver does not converge - an unreachable pose, or
/// one only reachable through a configuration far from the seed. The caller
/// should say so rather than sending something that was not asked for.
std::optional<QList<double>> inverseKinematics(const EePose &target,
                                               const QList<double> &seed);

}  // namespace hmi::robot
