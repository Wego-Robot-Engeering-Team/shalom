"""map 프레임(미터) ↔ QGraphicsScene 좌표(픽셀) 변환.

좌표계 정의
-----------
world : ROS map 프레임. x 우, y 상, theta 는 +x 축에서 CCW(반시계) 라디안.
scene : Qt 씬. x 우, y 하. 픽셀 1 = 격자 셀 1.
        씬 원점 (0,0) 은 맵 이미지의 좌상단.

OccupancyGrid 의 origin 은 셀 (0,0) = 격자 **좌하단** 모서리의 world 좌표다.
반면 이미지 행 0 은 **상단**이다(브릿지가 PNG 인코딩 시 상하 반전해서 보낸다,
docs/bridge_protocol.md §2.2). 따라서 y 축은 뒤집힌다:

    sx = (x - ox) / r
    sy = H - (y - oy) / r          # H = 격자 높이(셀)

역변환:

    x = ox + sx * r
    y = oy + (H - sy) * r

회전 부호
---------
world 각 theta 의 단위벡터는 (cos θ, sin θ). 씬은 y 가 아래로 향하므로
같은 방향이 씬에서는 (cos θ, -sin θ) 로 나타난다.
Qt 의 setRotation(a) 는 (1,0) 을 (cos a, sin a) 로 보내므로
    (cos a, sin a) = (cos θ, -sin θ)  →  a = -θ
즉 `item.setRotation(-degrees(theta))` 가 맞다. 부호를 뒤집지 말 것.
"""

from __future__ import annotations

import math
from dataclasses import dataclass

from PySide6.QtCore import QPointF


@dataclass(frozen=True)
class MapInfo:
    """OccupancyGrid 메타데이터."""

    width: int          # 셀
    height: int         # 셀
    resolution: float   # m / 셀
    origin_x: float     # 좌하단 모서리의 world x [m]
    origin_y: float     # 좌하단 모서리의 world y [m]
    origin_theta: float = 0.0
    map_id: str = ""

    def __post_init__(self) -> None:
        if abs(self.origin_theta) > 1e-6:
            # v1 미지원. 브릿지가 회전을 흡수해서 발행해야 한다.
            raise ValueError(
                f"origin_theta={self.origin_theta} — 회전된 맵은 프로토콜 v1 미지원"
            )
        if self.resolution <= 0:
            raise ValueError(f"resolution 은 양수여야 한다: {self.resolution}")

    # ---- world → scene ------------------------------------------------
    def to_scene(self, x: float, y: float) -> QPointF:
        return QPointF(
            (x - self.origin_x) / self.resolution,
            self.height - (y - self.origin_y) / self.resolution,
        )

    def to_scene_xy(self, x: float, y: float) -> tuple[float, float]:
        return (
            (x - self.origin_x) / self.resolution,
            self.height - (y - self.origin_y) / self.resolution,
        )

    # ---- scene → world ------------------------------------------------
    def to_world(self, sx: float, sy: float) -> tuple[float, float]:
        return (
            self.origin_x + sx * self.resolution,
            self.origin_y + (self.height - sy) * self.resolution,
        )

    # ---- 각도 ----------------------------------------------------------
    @staticmethod
    def theta_to_item_rotation(theta: float) -> float:
        """world theta [rad] → QGraphicsItem.setRotation() 인자 [deg]."""
        return -math.degrees(theta)

    @staticmethod
    def item_rotation_to_theta(deg: float) -> float:
        return -math.radians(deg)

    # ---- 편의 ----------------------------------------------------------
    @property
    def scene_width(self) -> float:
        return float(self.width)

    @property
    def scene_height(self) -> float:
        return float(self.height)

    def meters_to_px(self, m: float) -> float:
        return m / self.resolution


def normalize_angle(a: float) -> float:
    """(-pi, pi] 로 정규화."""
    return math.atan2(math.sin(a), math.cos(a))
