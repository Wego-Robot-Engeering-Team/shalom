#pragma once

// Robot hardware constants.
//
// WARNING: these must be checked against the actual URDF/xacro during
// integration. The values below come from the published Franka FR3
// specification; calibration after the arm is mounted may narrow the soft
// limits further. The robot-side arm node is authoritative - the control
// station only constrains the operator's input range to help, and never
// decides whether a motion is admissible (protocol section 4).

#include <array>

#include <QLatin1String>

namespace gcs::robot {

struct Joint {
    QLatin1String name;
    QLatin1String label;
    double lo;       ///< rad
    double hi;       ///< rad
    double velMax;   ///< rad/s
};

/// Franka FR3, seven revolute joints.
inline const std::array<Joint, 7> kFr3Joints{{
    {QLatin1String("fr3_joint1"), QLatin1String("J1"), -2.7437, 2.7437, 2.62},
    {QLatin1String("fr3_joint2"), QLatin1String("J2"), -1.7837, 1.7837, 2.62},
    {QLatin1String("fr3_joint3"), QLatin1String("J3"), -2.9007, 2.9007, 2.62},
    {QLatin1String("fr3_joint4"), QLatin1String("J4"), -3.0421, -0.1518, 2.62},
    {QLatin1String("fr3_joint5"), QLatin1String("J5"), -2.8065, 2.8065, 5.26},
    {QLatin1String("fr3_joint6"), QLatin1String("J6"), 0.5445, 4.5169, 4.18},
    {QLatin1String("fr3_joint7"), QLatin1String("J7"), -3.0159, 3.0159, 5.26},
}};

/// Named arm postures. Final values are set during on-site calibration.
inline const std::array<double, 7> kArmHome{{0.0, -0.785, 0.0, -2.356, 0.0, 1.571, 0.785}};
inline const std::array<double, 7> kArmStandby{{0.0, -0.400, 0.0, -1.900, 0.0, 1.500, 0.785}};
inline const std::array<double, 7> kArmStow{{0.0, -1.700, 0.0, -2.900, 0.0, 1.000, 0.785}};

/// B2 travel limits. The manual jog ceiling matches the 30 cm/s figure the
/// statement of work requires when an unmapped obstacle is nearby (2.2.5).
inline constexpr double kVxMax = 0.60;      ///< m/s
inline constexpr double kVyMax = 0.40;      ///< m/s
inline constexpr double kWzMax = 0.80;      ///< rad/s
inline constexpr double kVxCaution = 0.30;  ///< m/s, the mandated reduced speed

/// Manipulability thresholds, normalised. Re-tune after on-site trials.
inline constexpr double kManipWarn = 0.35;
inline constexpr double kManipDanger = 0.15;

/// Typical peak Yoshikawa index for the FR3, used to normalise the gauge.
inline constexpr double kManipNominal = 0.12;

}  // namespace gcs::robot
