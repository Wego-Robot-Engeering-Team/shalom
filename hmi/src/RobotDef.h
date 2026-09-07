#pragma once

// Robot hardware constants.
//
// WARNING: these must be checked against the actual URDF/xacro during
// integration. The arm values below are read off FAIRINO's own
// `fairino3_v6.urdf` (FAIR-INNOVATION/frcobot_ros2, package
// `fairino_description`); calibration after the arm is mounted may narrow the
// soft limits further. The robot-side arm node is authoritative - the control
// station only constrains the operator's input range to help, and never
// decides whether a motion is admissible (protocol section 4).

#include <array>
#include <cmath>

#include <QLatin1String>

namespace hmi::robot {

/// FAIRINO FR3: six revolute joints, 3 kg payload, 622 mm reach.
///
/// Not the Franka arm of the same name. The two are different robots that
/// share a model number - Franka's has seven axes and an 855 mm reach - and
/// the station was originally written against Franka's. Everything arm-shaped
/// in this file, in robot/Kinematics.cpp and in widgets/Robot3DView.cpp comes
/// from the FAIRINO URDF now.
inline constexpr int kArmJointCount = 6;

struct Joint {
    QLatin1String name;
    QLatin1String label;
    double lo;       ///< rad
    double hi;       ///< rad
    double velMax;   ///< rad/s
};

inline const std::array<Joint, kArmJointCount> kFr3Joints{{
    {QLatin1String("j1"), QLatin1String("J1"), -3.0543, 3.0543, 3.15},
    {QLatin1String("j2"), QLatin1String("J2"), -4.6251, 1.4835, 3.15},
    {QLatin1String("j3"), QLatin1String("J3"), -2.8274, 2.8274, 3.15},
    {QLatin1String("j4"), QLatin1String("J4"), -4.6251, 1.4835, 3.20},
    {QLatin1String("j5"), QLatin1String("J5"), -3.0543, 3.0543, 3.20},
    {QLatin1String("j6"), QLatin1String("J6"), -3.0543, 3.0543, 3.20},
}};

/// One link of the kinematic chain: where the joint sits relative to the
/// previous joint's frame, before that joint rotates.
///
/// This is the arm's shape, and it lives here alone. Kinematics.cpp,
/// PoseCheck.cpp and Robot3DView.cpp used to each carry their own copy of a
/// Denavit-Hartenberg table with a comment warning that the copies must not
/// diverge; a single table they all read cannot.
///
/// Every FR3 joint turns about its own z, so the axis is not stored.
struct ArmLink {
    double x, y, z;           ///< origin offset, m
    double roll, pitch, yaw;  ///< origin rotation, rad
};

inline const std::array<ArmLink, kArmJointCount> kFr3Chain{{
    {0.0,      0.0, 0.0,    0.0,          0.0, 0.0},
    {0.0,      0.0, 0.14,   M_PI_2,       0.0, 0.0},
    {-0.28,    0.0, 0.0,    0.0,          0.0, 0.0},
    {-0.24001, 0.0, 0.0,    0.0,          0.0, 0.0},
    {0.0,      0.0, 0.102,  M_PI_2,       0.0, 0.0},
    {0.0,      0.0, 0.102,  -M_PI_2,      0.0, 0.0},
}};

/// Named arm postures. Final values are set during on-site calibration.
///
/// Searched over the URDF chain rather than picked by eye, against four
/// conditions that a preset has to meet to be worth having
/// (`rl_training/tools` in the b2_simulation repo holds the scripts):
///
///   * inside the travel limits, with margin, so the pose check stays quiet;
///   * manipulability at least 35% of this arm's peak, so a preset never parks
///     the arm somewhere the operator immediately gets a singularity warning;
///   * no joint origin below the mounting plane, so no link is inside the deck;
///   * the flange clear of B2's LiDAR mast - the cylinder at x = 0.342,
///     z = 0.079..0.239, r = 0.076 in the MJCF - because that scan is what the
///     SLAM stack runs on.
///
///                flange, arm base frame        arm COM z   radius   manip
///     stow       (+0.273, -0.102, +0.066)        +0.098    0.074     37%
///     standby    (+0.221, -0.102, +0.292)        +0.159    0.063     36%
///     home       (+0.248, -0.102, +0.663)        +0.217    0.071     36%
///
/// The -0.102 m lateral offset is the arm's own shape, not a mounting error:
/// the two wrist links are offset like a UR arm's, so the flange never sits on
/// the j1 axis.
///
/// Closest approach to the LiDAR is `stow`, at 81 mm. That is the pose the
/// robot walks in, so it is also the one worth re-measuring against the real
/// bracket first.
///
/// `stow` matters most: it is the travel pose, the one that keeps the arm's
/// mass over the trunk, and the pose the walking policy was trained around.
inline const std::array<double, kArmJointCount> kArmHome{
    {0.0, -1.600, -0.800, -1.400, -1.571, 0.0}};
inline const std::array<double, kArmJointCount> kArmStandby{
    {0.0, -1.200, -2.200, -1.800, -1.571, 0.0}};
inline const std::array<double, kArmJointCount> kArmStow{
    {0.0, -2.600, -2.400, 0.400, -1.571, 0.0}};

/// Where the FR3 bolts onto B2's back, in base_link coordinates, and the trunk
/// it has to avoid.
///
/// Both are read off the MJCF the robot is simulated and trained with
/// (`b2_simulation/mujoco/b2_mujoco/models/b2.xml`): `base1_collision` is a
/// 0.50 x 0.28 x 0.15 m box centred on the body origin, and the mount is the
/// deck that `rl_training/tools/make_b2_arm_asset.py` puts the payload on.
/// They live here because the pose check and the 3D view both need them and
/// must agree - a warning that fires where the picture shows clearance teaches
/// the operator to ignore warnings.
struct Box {
    double minX, maxX, minY, maxY, minZ, maxZ;   ///< m
};

inline constexpr double kArmMountX = -0.05;
inline constexpr double kArmMountY = 0.0;
inline constexpr double kArmMountZ = 0.13;

/// The trunk, expressed in the *arm base* frame - the frame the pose check and
/// forward kinematics both work in.
inline constexpr Box kB2TrunkInArmFrame{
    -0.25 - kArmMountX, 0.25 - kArmMountX,
    -0.14 - kArmMountY, 0.14 - kArmMountY,
    -1.0,               0.075 - kArmMountZ,
};

/// B2 travel limits. The manual jog ceiling matches the 30 cm/s figure the
/// statement of work requires when an unmapped obstacle is nearby (2.2.5).
inline constexpr double kVxMax = 0.60;      ///< m/s
inline constexpr double kVyMax = 0.40;      ///< m/s
inline constexpr double kWzMax = 0.80;      ///< rad/s
inline constexpr double kVxCaution = 0.30;  ///< m/s, the mandated reduced speed

/// Robot-side safety timings, in the units the operator reads them in.
///
/// The station only displays these. The robot enforces them, and the safety
/// node does so without going through this protocol at all - see protocol
/// sections 4 and 5. They are named here rather than written into the settings
/// screen so that the number on screen and the number in the specification
/// cannot drift apart unnoticed; test_docs checks both against the document.
inline constexpr int kEstopResponseSec = 1;    ///< engage to full stop
inline constexpr int kLinkLossStopSec = 3;     ///< heartbeat loss to stop in place
inline constexpr int kDeadmanMs = 300;         ///< jog command timeout

/// Manipulability thresholds, normalised. Re-tune after on-site trials.
inline constexpr double kManipWarn = 0.35;
inline constexpr double kManipDanger = 0.15;

/// Peak Yoshikawa index for this arm, used to normalise the gauge.
///
/// Measured, not quoted: the index is |det J| for a six-axis arm, and a sweep
/// of 117649 poses followed by a local search over the URDF chain peaks at
/// 0.0335. The 0.12 that stood here was Franka's figure for a seven-axis arm,
/// and would have made the gauge read about a quarter of the truth.
inline constexpr double kManipNominal = 0.0335;

}  // namespace hmi::robot
