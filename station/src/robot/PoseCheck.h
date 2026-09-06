#pragma once

// Pre-flight check on an arm pose the operator has dialled in but not sent.
//
// The robot remains the authority: protocol section 4 puts singularity and
// collision handling on the robot side, and it will refuse or stop on its own.
// This only spares the operator from finding that out after pressing send,
// when the only feedback is a rejection code and an arm that did not move.
//
// The checks are deliberately cheap and local - joint limits, the two FR3
// configurations that lose a degree of freedom, and whether forward kinematics
// puts a link inside the B2 body. No inverse kinematics, no mesh collision:
// approximating those here would produce a second opinion that disagrees with
// the robot, and the operator would have no way to tell which one is right.

#include <QList>
#include <QVector3D>
#include <QString>

namespace gcs::robot {

/// What a pose check found. severity is "" when there is nothing to say.
struct PoseWarning {
    QString severity;   ///< "" | "warn" | "danger"
    QString text;       ///< one line, already operator-readable

    bool isEmpty() const { return severity.isEmpty(); }
};

/// Checks seven joint angles in radians. Returns the single most serious
/// finding: listing three at once buries the one that matters.
PoseWarning checkArmPose(const QList<double> &joints);

}  // namespace gcs::robot
