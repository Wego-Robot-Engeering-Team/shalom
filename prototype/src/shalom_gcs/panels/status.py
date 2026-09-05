"""상태 모니터링 패널 — 과업지시서 2.2.7 [5]."""

from __future__ import annotations

from PySide6.QtCore import Qt
from PySide6.QtWidgets import QGridLayout, QHBoxLayout, QLabel, QVBoxLayout, QWidget

from ..theme.tokens import METRICS as M
from ..widgets.gauges import BatteryRing, StatBar
from ..widgets.primitives import Badge, Card, readout, section_label


class StatusPanel(Card):
    def __init__(self, parent: QWidget | None = None):
        super().__init__("상태 모니터링", parent)

        self.conn_badge = Badge("연결 끊김", "danger")
        self.add_header_widget(self.conn_badge)

        # ---- 배터리 + 모드 ----
        top = QHBoxLayout()
        top.setSpacing(M.s4)
        self.battery = BatteryRing(size=86, low_threshold=25.0)
        top.addWidget(self.battery, 0, Qt.AlignVCenter)

        right = QVBoxLayout()
        right.setSpacing(M.s2)
        right.addWidget(section_label("주행 모드"))
        self.mode_badge = Badge("—", "neutral")
        right.addWidget(self.mode_badge, 0, Qt.AlignLeft)
        right.addWidget(section_label("Apriltag"))
        self.tag_badge = Badge("미인식", "neutral")
        right.addWidget(self.tag_badge, 0, Qt.AlignLeft)
        right.addStretch(1)
        top.addLayout(right, 1)
        self.body.addLayout(top)

        # ---- 시스템 지표 ----
        self.body.addSpacing(M.s2)
        self.body.addWidget(section_label("시스템"))
        grid = QGridLayout()
        grid.setContentsMargins(0, 0, 0, 0)
        grid.setHorizontalSpacing(M.s4)
        grid.setVerticalSpacing(M.s1)
        self.cpu = StatBar("CPU", "%", warn_above=80, danger_above=92)
        self.mem = StatBar("MEM", "%", warn_above=80, danger_above=92)
        self.cpu_t = StatBar("CPU 온도", "°C", warn_above=75, danger_above=88, vmax=100)
        self.gpu_t = StatBar("GPU 온도", "°C", warn_above=75, danger_above=88, vmax=100)
        self.rtt = StatBar("링크 RTT", "ms", warn_above=120, danger_above=400, vmax=500)
        grid.addWidget(self.cpu,   0, 0)
        grid.addWidget(self.mem,   0, 1)
        grid.addWidget(self.cpu_t, 1, 0)
        grid.addWidget(self.gpu_t, 1, 1)
        grid.addWidget(self.rtt,   2, 0, 1, 2)
        self.body.addLayout(grid)

        # ---- 위치 리드아웃 ----
        self.body.addSpacing(M.s2)
        self.body.addWidget(section_label("현재 위치 (map)"))
        self.pose_lbl = readout("—")
        self.body.addWidget(self.pose_lbl)
        self.body.addStretch(1)

    # ---- 갱신 ----
    def set_connected(self, ok: bool) -> None:
        self.conn_badge.set("연결됨" if ok else "연결 끊김", "ok" if ok else "danger")

    def set_battery(self, soc: float, charging: bool = False) -> None:
        self.battery.set_state(soc, charging)

    def set_mode(self, mode: str, estop: bool) -> None:
        if estop:
            self.mode_badge.set("E-STOP", "danger")
        elif mode == "auto":
            self.mode_badge.set("자율", "info")
        elif mode == "manual":
            self.mode_badge.set("수동", "warn")
        else:
            self.mode_badge.set(mode or "—", "neutral")

    def set_system(self, cpu=None, mem=None, cpu_t=None, gpu_t=None, rtt=None) -> None:
        self.cpu.set_value(cpu)
        self.mem.set_value(mem)
        self.cpu_t.set_value(cpu_t)
        self.gpu_t.set_value(gpu_t)
        self.rtt.set_value(rtt)

    def set_pose(self, x: float, y: float, theta_deg: float) -> None:
        self.pose_lbl.setText(f"{x:+7.2f}, {y:+7.2f}   θ {theta_deg:+6.1f}°")

    def set_tags(self, n_seen: int) -> None:
        if n_seen > 0:
            self.tag_badge.set(f"{n_seen}개 인식", "ok")
        else:
            self.tag_badge.set("미인식", "neutral")
