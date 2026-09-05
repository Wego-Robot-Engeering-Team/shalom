"""데모/개발용 합성 데이터 생성기.

브릿지 없이 UI 를 띄우고 레이아웃·렌더링을 검증하기 위한 것이다.
실제 브릿지가 붙으면 net.client 가 이 자리를 대체한다.
검수 산출물이 아니라 개발 도구다 — 납품 빌드에서는 제외한다.

여기서 만드는 맵은 GTX-A 검수고를 단순화한 형상이다:
중앙에 열차 하부 점검 피트가 있고 양옆에 통로가 있는 구조.
"""

from __future__ import annotations

import math
import random

import numpy as np

from .mapview.transform import MapInfo

RESOLUTION = 0.05     # 5 cm/셀
W, H = 720, 420       # 36 m x 21 m


def build_map() -> tuple[MapInfo, np.ndarray]:
    g = np.full((H, W), -1, dtype=np.int8)     # 미탐색으로 시작

    # 검수고 내부 자유 공간
    g[30:H - 30, 30:W - 30] = 0

    # 외벽
    for (y0, y1, x0, x1) in [
        (30, 38, 30, W - 30), (H - 38, H - 30, 30, W - 30),
        (30, H - 30, 30, 38), (30, H - 30, W - 38, W - 30),
    ]:
        g[y0:y1, x0:x1] = 100

    # 열차 하부 점검 피트 — 좌우 벽 두 줄. 그 사이가 로봇 주행로.
    pit_y0, pit_y1 = 150, 270
    for x0 in range(90, W - 90, 1):
        g[pit_y0:pit_y0 + 6, x0] = 100
        g[pit_y1:pit_y1 + 6, x0] = 100

    # 량 구분 기둥
    for cx in range(120, W - 100, 118):
        g[pit_y0 - 26:pit_y0, cx:cx + 10] = 100
        g[pit_y1 + 6:pit_y1 + 32, cx:cx + 10] = 100

    # 충전 스테이션
    g[60:96, 60:120] = 100
    g[66:90, 66:114] = 0

    # SLAM 특유의 지저분한 경계 재현
    rng = np.random.default_rng(7)
    noise = rng.random((H, W)) < 0.0012
    edge = (g == 0) & noise
    g[edge] = rng.integers(25, 70, size=int(edge.sum()), dtype=np.int8)

    info = MapInfo(
        width=W, height=H, resolution=RESOLUTION,
        origin_x=-W * RESOLUTION / 2, origin_y=-H * RESOLUTION / 2,
        map_id="gtxa_pit_demo",
    )
    return info, g


def build_waypoints(info: MapInfo) -> list[dict]:
    """피트 내부를 따라 배치한 점검포인트. 량당 4포인트 x 3량."""
    wps: list[dict] = []
    y = 0.0
    n = 0
    for car in range(1, 4):
        for pt in range(1, 5):
            x = -14.0 + n * 2.35
            wps.append({
                "id": f"C{car:02d}-P{pt:02d}",
                "name": f"{car}량 P{pt}",
                "x": x, "y": y, "theta": 0.0,
                "tag_id": 10 + n,
                "status": "todo",
            })
            n += 1
    return wps


def build_tags(wps: list[dict]) -> list[dict]:
    """각 포인트 양옆 벽에 부착된 Apriltag."""
    tags = []
    for wp in wps:
        tags.append({"id": wp["tag_id"], "x": wp["x"], "y": 3.0})
        tags.append({"id": wp["tag_id"] + 100, "x": wp["x"], "y": -3.0})
    return tags


class DemoFeed:
    """시간에 따라 변하는 합성 텔레메트리."""

    def __init__(self, wps: list[dict]):
        self.t = 0.0
        self.wps = wps
        self.idx = 0
        self.trail: list[tuple[float, float]] = []
        self.soc = 87.0
        self.joints = [0.0, -0.785, 0.0, -2.356, 0.0, 1.571, 0.785]

    def step(self, dt: float) -> dict:
        self.t += dt

        # 피트를 따라 왕복 주행
        span = 26.0
        x = -13.0 + span * (0.5 - 0.5 * math.cos(self.t * 0.09))
        y = 0.35 * math.sin(self.t * 0.5)
        theta = math.atan2(0.35 * 0.5 * math.cos(self.t * 0.5),
                           span * 0.5 * 0.09 * math.sin(self.t * 0.09) + 1e-6)

        self.trail.append((x, y))
        if len(self.trail) > 900:
            self.trail.pop(0)

        # 진행 상황: 로봇이 지나간 포인트를 완료 처리
        for i, wp in enumerate(self.wps):
            if wp["x"] < x - 0.6:
                wp["status"] = "done"
            elif abs(wp["x"] - x) <= 0.6:
                wp["status"] = "current"
                self.idx = i
            else:
                wp["status"] = "todo"
        self.wps[7]["status"] = "error" if self.wps[7]["status"] == "done" else self.wps[7]["status"]

        # 계획 경로: 현재 위치에서 남은 포인트들
        plan = [(x, y)] + [(w["x"], w["y"]) for w in self.wps if w["status"] == "todo"]

        self.soc = max(8.0, self.soc - dt * 0.045)

        # 관절: 촬영 자세를 오가며 조작성 지수가 오르내리게
        phase = math.sin(self.t * 0.35)
        self.joints = [
            0.35 * phase,
            -0.785 + 0.55 * phase,
            0.12 * math.sin(self.t * 0.22),
            -2.356 + 0.95 * abs(phase),        # 팔꿈치 폄 → 특이자세 근접
            0.10 * phase,
            1.571 + 0.40 * phase,
            0.785,
        ]
        # 4축이 펴질수록(0 에 가까울수록) 조작성 급감 — 팔꿈치 특이자세 모사
        elbow = abs(self.joints[3] + 0.1518) / 2.89
        manip = max(0.004, 0.115 * (elbow ** 0.7))
        sigma = max(0.002, 0.085 * (elbow ** 0.8))

        seen = {self.wps[self.idx]["tag_id"]} if self.t % 7 < 3.2 else set()

        return {
            "pose": (x, y, theta),
            "trail": list(self.trail),
            "plan": plan,
            "waypoints": self.wps,
            "soc": self.soc,
            "joints": self.joints,
            "manip": manip,
            "sigma": sigma,
            "seen_tags": seen,
            "cpu": 34 + 22 * abs(math.sin(self.t * 0.3)),
            "mem": 51 + 8 * abs(math.sin(self.t * 0.17)),
            "cpu_t": 58 + 14 * abs(math.sin(self.t * 0.11)),
            "gpu_t": 62 + 16 * abs(math.sin(self.t * 0.13)),
            "rtt": 18 + 14 * random.random(),
        }
