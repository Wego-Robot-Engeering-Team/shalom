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
// seven-axis arm has infinitely many ways to reach a pose. Treating this
// solution as authoritative would put a second planner in the control station,
// disagreeing with the robot's, with no way for the operator to tell which is
// right.
//
// It is a preview. That is why inverse() takes the current joints as a seed:
// among the many solutions it returns the one nearest where the arm already
// is, which is the one the operator is picturing.

#include <QList>
#include <optional>

namespace gcs::robot {

/// End-effector pose in the arm base frame. Metres and radians.
struct EePose {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double roll = 0.0;
    double pitch = 0.0;
    double yaw = 0.0;
};

/// Where the flange ends up for these seven joint angles.
EePose forwardKinematics(const QList<double> &joints);

/// Joint angles that put the flange at `target`, starting the search from
/// `seed` and staying inside the FR3 travel limits.
///
/// Returns nullopt when the solver does not converge - an unreachable pose, or
/// one only reachable through a configuration far from the seed. The caller
/// should say so rather than sending something that was not asked for.
std::optional<QList<double>> inverseKinematics(const EePose &target,
                                               const QList<double> &seed);

}  // namespace gcs::robot
