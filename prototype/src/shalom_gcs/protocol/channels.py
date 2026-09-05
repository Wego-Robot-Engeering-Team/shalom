"""채널 이름 상수. 문자열 오타를 컴파일 타임 가까이로 끌어온다."""

from __future__ import annotations

# ---- 상태 (브릿지 → GCS) ----
POSE = "state/pose"
BATTERY = "state/battery"
SYSTEM = "state/system"
SAFETY = "state/safety"
NAV = "state/nav"
PLAN = "state/plan"
TRAIL = "state/trail"
ARM = "state/arm"
APRILTAG = "state/apriltag"
MISSION = "state/mission"
WAYPOINTS = "state/waypoints"
LOG = "evt/log"
MAP = "map/occupancy"
PREVIEW = "capture/preview"

ALL_STATE = [
    POSE, BATTERY, SYSTEM, SAFETY, NAV, PLAN, TRAIL,
    ARM, APRILTAG, MISSION, WAYPOINTS, MAP, PREVIEW,
]

# ---- 명령 (GCS → 브릿지) ----
CMD_ESTOP = "cmd/estop"
CMD_ESTOP_RELEASE = "cmd/estop_release"
CMD_MODE = "cmd/mode"
CMD_GOTO = "cmd/goto"
CMD_NAV_CANCEL = "cmd/nav_cancel"
CMD_WAYPOINTS_SET = "cmd/waypoints/set"
CMD_MISSION_START = "cmd/mission/start"
CMD_MISSION_PAUSE = "cmd/mission/pause"
CMD_MISSION_RESUME = "cmd/mission/resume"
CMD_MISSION_STOP = "cmd/mission/stop"
CMD_ARM_PRESET = "cmd/arm/preset"
CMD_ARM_JOINT_GOAL = "cmd/arm/joint_goal"
CMD_ARM_EE_GOAL = "cmd/arm/ee_goal"
CMD_ARM_STOP = "cmd/arm/stop"
CMD_CAPTURE = "cmd/capture/trigger"
CMD_VEL = "cmd/cmd_vel"      # pub, 20 Hz, 데드맨 300ms
