"""OccupancyGrid → QImage 변환.

브릿지는 PNG 로 보내주지만(프로토콜 §2.2), 원시 격자를 직접 색칠해야 하는
경로(로컬 맵 파일 로드, 시뮬레이션)도 있어 공통 렌더러를 둔다.

ROS OccupancyGrid 값 규약: -1 = 미탐색, 0 = 자유, 100 = 점유.
"""

from __future__ import annotations

from ..theme.tokens import mono_family, palette

import numpy as np
from PySide6.QtGui import QColor, QImage



def _rgb(hex_color: str) -> tuple[int, int, int]:
    c = QColor(hex_color)
    return c.red(), c.green(), c.blue()


def occupancy_to_image(grid: np.ndarray, *, occupied_threshold: int = 65) -> QImage:
    """grid: (H, W) int8, 행 0 = 이미지 상단(이미 상하 반전된 상태).

    반환 QImage 는 내부 버퍼를 복사해 소유한다 — numpy 배열이 GC 되어도 안전하다.
    """
    if grid.ndim != 2:
        raise ValueError(f"2차원 격자여야 한다: shape={grid.shape}")

    P = palette()
    h, w = grid.shape
    rgb = np.empty((h, w, 3), dtype=np.uint8)

    unknown = grid < 0
    occupied = grid >= occupied_threshold
    free = ~unknown & ~occupied

    rgb[unknown] = _rgb(P.map_unknown)
    rgb[free] = _rgb(P.map_free)
    rgb[occupied] = _rgb(P.map_occupied)

    # 점유도 중간값은 자유↔점유 사이를 보간해 부드럽게 (SLAM 신뢰도 표현)
    mid = ~unknown & ~occupied & (grid > 20)
    if mid.any():
        t = (grid[mid].astype(np.float32) - 20) / (occupied_threshold - 20)
        f = np.array(_rgb(P.map_free), dtype=np.float32)
        o = np.array(_rgb(P.map_occupied), dtype=np.float32)
        rgb[mid] = (f + (o - f) * t[:, None]).astype(np.uint8)

    buf = np.ascontiguousarray(rgb)
    img = QImage(buf.data, w, h, w * 3, QImage.Format_RGB888)
    return img.copy()
