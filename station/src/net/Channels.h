#pragma once

// Channel names. See docs/bridge_protocol.md sections 2 and 3.
//
// Naming these keeps channel-string typos close to compile time; an
// unrecognised channel is silently ignored by both peers for forward
// compatibility, so a typo would otherwise produce a topic that never fires.

namespace gcs::ch {

// ---- State, published by the bridge -------------------------------------
inline constexpr auto kPose = "state/pose";           ///< 10 Hz
inline constexpr auto kBattery = "state/battery";     ///< 1 Hz
inline constexpr auto kSystem = "state/system";       ///< 1 Hz
inline constexpr auto kSafety = "state/safety";       ///< on change + 1 Hz
inline constexpr auto kNav = "state/nav";             ///< 5 Hz
inline constexpr auto kPlan = "state/plan";           ///< on change
inline constexpr auto kTrail = "state/trail";         ///< 2 Hz
inline constexpr auto kArm = "state/arm";             ///< 10 Hz
inline constexpr auto kApriltag = "state/apriltag";   ///< on detection
inline constexpr auto kMission = "state/mission";     ///< on change
inline constexpr auto kWaypoints = "state/waypoints"; ///< on change
inline constexpr auto kLog = "evt/log";               ///< event
inline constexpr auto kMap = "map/occupancy";         ///< binary, on change
inline constexpr auto kPreview = "capture/preview";   ///< binary, on capture

/// Upload backlog for captured originals. Those files go straight from the
/// robot to the NAS (protocol section 6), so this channel is the operator's
/// only way to tell whether an inspection run is actually finished.
inline constexpr auto kCaptureSpool = "state/capture_spool";

/// Sensor and link health. Staleness is judged by the bridge against each
/// sensor's expected rate, not by a fixed timeout here (protocol section 9.2).
inline constexpr auto kHealth = "state/health";

// ---- Commands, sent by the control station -------------------------------
inline constexpr auto kCmdEstop = "cmd/estop";                  ///< engage only
inline constexpr auto kCmdEstopRelease = "cmd/estop_release";   ///< manual release only
inline constexpr auto kCmdMode = "cmd/mode";
inline constexpr auto kCmdGoto = "cmd/goto";
inline constexpr auto kCmdNavCancel = "cmd/nav_cancel";
inline constexpr auto kCmdWaypointsSet = "cmd/waypoints/set";   ///< replaces the whole list

/// Battery policy the robot must enforce: return_at and depart_at, in percent.
///
/// Sent whenever the setting changes and once on connect, so a robot that
/// rebooted picks the policy back up. The robot keeps the last value it was
/// given; it must not fall back to a built-in default silently, because the
/// operator would then be looking at a number the robot is not using.
inline constexpr auto kCmdPowerPolicy = "cmd/power/policy";
inline constexpr auto kCmdMissionStart = "cmd/mission/start";
inline constexpr auto kCmdMissionPause = "cmd/mission/pause";
inline constexpr auto kCmdMissionResume = "cmd/mission/resume"; ///< explicit resume only
inline constexpr auto kCmdMissionStop = "cmd/mission/stop";
inline constexpr auto kCmdArmPreset = "cmd/arm/preset";
inline constexpr auto kCmdArmJointGoal = "cmd/arm/joint_goal";
inline constexpr auto kCmdArmEeGoal = "cmd/arm/ee_goal";
inline constexpr auto kCmdArmStop = "cmd/arm/stop";
inline constexpr auto kCmdCapture = "cmd/capture/trigger";

/// Published at 20 Hz while the operator holds a jog control. The bridge
/// latches zero velocity if it stops arriving for 300 ms, so a frozen or
/// disconnected control station cannot leave the robot driving.
inline constexpr auto kCmdVel = "cmd/cmd_vel";

}  // namespace gcs::ch
