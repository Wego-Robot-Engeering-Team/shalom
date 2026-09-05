"""로봇 하드웨어 정의 상수.

⚠️ 통합 시점에 반드시 실제 URDF/xacro 와 대조해 검증할 것.
여기 값은 Franka FR3 공개 사양 기준이며, 로봇암 장착 후 캘리브레이션 결과에
따라 소프트 리밋이 더 좁아질 수 있다. 최종 권위는 로봇측 암 노드에 있고,
UI 는 조작 범위를 제한해 조작자를 돕는 역할만 한다
(docs/bridge_protocol.md §4 — 한계 판정은 로봇측 책임).
"""

from __future__ import annotations

import math
from dataclasses import dataclass


@dataclass(frozen=True)
class Joint:
    name: str
    label: str
    lo: float      # rad
    hi: float      # rad
    vel_max: float # rad/s


# Franka FR3 7축 관절 한계
FR3_JOINTS: tuple[Joint, ...] = (
    Joint("fr3_joint1", "J1", -2.7437, 2.7437, 2.62),
    Joint("fr3_joint2", "J2", -1.7837, 1.7837, 2.62),
    Joint("fr3_joint3", "J3", -2.9007, 2.9007, 2.62),
    Joint("fr3_joint4", "J4", -3.0421, -0.1518, 2.62),
    Joint("fr3_joint5", "J5", -2.8065, 2.8065, 5.26),
    Joint("fr3_joint6", "J6",  0.5445, 4.5169, 4.18),
    Joint("fr3_joint7", "J7", -3.0159, 3.0159, 5.26),
)

# 프리셋 자세. 실제 값은 현장 캘리브레이션 후 확정한다.
ARM_PRESETS: dict[str, tuple[float, ...]] = {
    "home":    (0.0, -0.785, 0.0, -2.356, 0.0, 1.571, 0.785),
    "standby": (0.0, -0.400, 0.0, -1.900, 0.0, 1.500, 0.785),
    "stow":    (0.0, -1.700, 0.0, -2.900, 0.0, 1.000, 0.785),
}

# B2 주행 속도 한계.
# 지시서 2.2.5: 미등록 물체 접근 시 30 cm/s 로 감속 — 수동 조작 상한도 이에 맞춘다.
B2_VX_MAX = 0.60     # m/s
B2_VY_MAX = 0.40     # m/s
B2_WZ_MAX = 0.80     # rad/s
B2_VX_CAUTION = 0.30 # m/s — 감속 기준선

# 조작성 지수 경고 임계값. 현장 시험으로 재조정 대상.
MANIP_WARN = 0.35
MANIP_DANGER = 0.15


def joint_limits_deg(j: Joint) -> tuple[float, float]:
    return math.degrees(j.lo), math.degrees(j.hi)
