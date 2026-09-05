"""로봇팔(FR3) 제어 패널 — 과업지시서 2.2.7 [3].

  ① End-Effector 목표 위치·자세 입력 + MoveIt2 실행
  ② 각 관절 각도 실시간 표시 + 개별 관절 수동 제어
  ③ 프리셋(홈·촬영대기·수납)

싱귤래리티 취급 원칙
--------------------
UI 는 조작성 지수를 **표시**할 뿐, 특이자세 판정과 회피는 로봇측 암 노드가 한다
(docs/bridge_protocol.md §4). 이유:

  - 관절 슬라이더로 움직이는 조인트 공간 이동은 싱귤래리티와 무관하다.
    여기서 실제로 위험한 건 FR3 의 위치/속도/가속도/저크 한계이며,
    슬라이더 raw 값을 그대로 쏘면 libfranka 가 리플렉스로 튕겨낸다.
    그래서 UI 는 '목표 자세'를 보내고 궤적 생성은 로봇측이 한다.
  - 데카르트 목표(EE pose)는 IK 해 존재 여부를 로봇측에서 판정해 사유를 돌려준다.
    FR3 는 7자유도라 널스페이스가 있어 어깨/팔꿈치/손목 특이자세를
    자세 재구성으로 회피할 여지가 있는데, 그 판단은 MoveIt2 영역이다.

조작자에게는 "지금 팔이 뻗대고 있다"를 눈으로 보여주는 것이 목적이다.
"""

from __future__ import annotations

import math

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (
    QDoubleSpinBox,
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QSlider,
    QVBoxLayout,
    QWidget,
)

from ..robot_def import ARM_PRESETS, FR3_JOINTS, MANIP_DANGER, MANIP_WARN
from ..theme.tokens import METRICS as M
from ..widgets.gauges import ArcGauge, JointBar
from ..widgets.primitives import Badge, Card, HLine, readout, section_label

# 조작성 지수 정규화 기준. FR3 의 전형적 최대치로, 현장 시험 후 재조정한다.
MANIP_NOMINAL = 0.12


class ArmPanel(Card):
    joint_goal = Signal(list)        # [7] rad
    ee_goal = Signal(dict)
    preset = Signal(str)
    stop = Signal()

    def __init__(self, parent: QWidget | None = None):
        super().__init__("로봇팔 제어 · FR3", parent)
        self.state_badge = Badge("대기", "neutral")
        self.add_header_widget(self.state_badge)

        # ================= 싱귤래리티 게이지 =================
        gauge_row = QHBoxLayout()
        gauge_row.setSpacing(M.s3)
        self.manip = ArcGauge(caption="조작성 지수 w", warn_below=0.35, danger_below=0.15)
        gauge_row.addWidget(self.manip, 1)

        side = QVBoxLayout(); side.setSpacing(M.s1)
        side.addWidget(section_label("σ min"))
        self.sigma_lbl = readout("—", large=True)
        side.addWidget(self.sigma_lbl)
        self.sing_badge = Badge("정상", "ok")
        side.addWidget(self.sing_badge, 0, Qt.AlignLeft)
        side.addStretch(1)
        gauge_row.addLayout(side)
        self.body.addLayout(gauge_row)
        self.body.addWidget(HLine())

        # ================= 관절 =================
        self.body.addWidget(section_label("관절 (7축)"))
        self._sliders: list[QSlider] = []
        self._bars: list[JointBar] = []
        self._syncing = False

        for j in FR3_JOINTS:
            row = QWidget()
            rl = QVBoxLayout(row)
            rl.setContentsMargins(0, 0, 0, 0)
            rl.setSpacing(0)

            bar = JointBar(j.label, j.lo, j.hi)
            rl.addWidget(bar)
            self._bars.append(bar)

            sl = QSlider(Qt.Horizontal)
            # 0.1도 해상도의 정수 눈금으로 다룬다
            sl.setRange(int(math.degrees(j.lo) * 10), int(math.degrees(j.hi) * 10))
            sl.setValue(0 if j.lo < 0 < j.hi else int(math.degrees((j.lo + j.hi) / 2) * 10))
            sl.valueChanged.connect(self._on_slider)
            rl.addWidget(sl)
            self._sliders.append(sl)

            self.body.addWidget(row)

        jrow = QHBoxLayout(); jrow.setSpacing(M.s2)
        self.btn_send_joints = QPushButton("관절 목표 전송")
        self.btn_send_joints.setProperty("variant", "primary")
        self.btn_send_joints.setProperty("size", "sm")
        self.btn_sync = QPushButton("현재값 동기화")
        self.btn_sync.setProperty("size", "sm")
        jrow.addWidget(self.btn_send_joints, 1)
        jrow.addWidget(self.btn_sync)
        self.body.addLayout(jrow)
        self.btn_send_joints.clicked.connect(self._emit_joint_goal)
        self.btn_sync.clicked.connect(self._sync_sliders_to_actual)

        self.body.addWidget(HLine())

        # ================= End-Effector =================
        self.body.addWidget(section_label("End-Effector 목표 (MoveIt2)"))
        grid = QGridLayout()
        grid.setSpacing(M.s2)
        self._ee: dict[str, QDoubleSpinBox] = {}
        specs = [
            ("x", "X [m]", -1.0, 1.0, 0.01, 0.40),
            ("y", "Y [m]", -1.0, 1.0, 0.01, 0.00),
            ("z", "Z [m]", -0.5, 1.5, 0.01, 0.50),
            ("roll", "R [°]", -180, 180, 1.0, 180.0),
            ("pitch", "P [°]", -180, 180, 1.0, 0.0),
            ("yaw", "Y [°]", -180, 180, 1.0, 0.0),
        ]
        for i, (key, label, lo, hi, step, default) in enumerate(specs):
            lbl = QLabel(label)
            lbl.setObjectName("SectionLabel")
            sb = QDoubleSpinBox()
            sb.setRange(lo, hi); sb.setSingleStep(step); sb.setValue(default)
            sb.setDecimals(2 if step < 1 else 1)
            sb.setAlignment(Qt.AlignRight)
            grid.addWidget(lbl, i // 3 * 2, i % 3)
            grid.addWidget(sb, i // 3 * 2 + 1, i % 3)
            self._ee[key] = sb
        self.body.addLayout(grid)

        self.btn_send_ee = QPushButton("EE 목표 실행")
        self.btn_send_ee.setProperty("variant", "primary")
        self.btn_send_ee.setProperty("size", "sm")
        self.btn_send_ee.clicked.connect(self._emit_ee_goal)
        self.body.addWidget(self.btn_send_ee)

        self.body.addWidget(HLine())

        # ================= 프리셋 / 정지 =================
        self.body.addWidget(section_label("프리셋"))
        pr = QHBoxLayout(); pr.setSpacing(M.s2)
        for key, label in (("home", "홈"), ("standby", "촬영대기"), ("stow", "수납")):
            b = QPushButton(label)
            b.setProperty("size", "sm")
            b.clicked.connect(lambda _c=False, k=key: self.preset.emit(k))
            pr.addWidget(b, 1)
        self.body.addLayout(pr)

        self.btn_stop = QPushButton("로봇팔 정지")
        self.btn_stop.setProperty("variant", "danger")
        self.btn_stop.clicked.connect(self.stop)
        self.body.addWidget(self.btn_stop)
        self.body.addStretch(1)

        self._actual: list[float] = [0.0] * len(FR3_JOINTS)

    # ================= 상태 수신 =================
    def set_arm_state(self, positions: list[float], manipulability: float,
                      sigma_min: float, moveit_state: str = "idle") -> None:
        self._actual = list(positions)
        for bar, q in zip(self._bars, positions):
            bar.set_actual(q)

        norm = max(0.0, min(1.0, manipulability / MANIP_NOMINAL))
        self.manip.set_state(norm, f"{manipulability:.4f}")
        self.sigma_lbl.setText(f"{sigma_min:.4f}")

        if norm <= MANIP_DANGER:
            self.sing_badge.set("특이자세 근접", "danger")
        elif norm <= MANIP_WARN:
            self.sing_badge.set("주의", "warn")
        else:
            self.sing_badge.set("정상", "ok")

        label = {"idle": "대기", "planning": "계획 중",
                 "executing": "실행 중", "error": "오류"}.get(moveit_state, moveit_state)
        tone = {"idle": "neutral", "planning": "info",
                "executing": "info", "error": "danger"}.get(moveit_state, "neutral")
        self.state_badge.set(label, tone)

    def set_enabled(self, on: bool) -> None:
        for sl in self._sliders:
            sl.setEnabled(on)
        for b in (self.btn_send_joints, self.btn_send_ee, self.btn_sync):
            b.setEnabled(on)

    # ================= 내부 =================
    def _on_slider(self, _v: int) -> None:
        if self._syncing:
            return
        for bar, sl in zip(self._bars, self._sliders):
            bar.set_command(math.radians(sl.value() / 10.0))

    def _sync_sliders_to_actual(self) -> None:
        self._syncing = True
        for sl, q in zip(self._sliders, self._actual):
            sl.setValue(int(math.degrees(q) * 10))
        self._syncing = False
        for bar in self._bars:
            bar.set_command(None)

    def _emit_joint_goal(self) -> None:
        self.joint_goal.emit([math.radians(sl.value() / 10.0) for sl in self._sliders])

    def _emit_ee_goal(self) -> None:
        self.ee_goal.emit({
            "x": self._ee["x"].value(),
            "y": self._ee["y"].value(),
            "z": self._ee["z"].value(),
            "roll": math.radians(self._ee["roll"].value()),
            "pitch": math.radians(self._ee["pitch"].value()),
            "yaw": math.radians(self._ee["yaw"].value()),
            "frame": "fr3_link0",
        })

    def apply_preset_to_sliders(self, name: str) -> None:
        vals = ARM_PRESETS.get(name)
        if not vals:
            return
        self._syncing = True
        for sl, q in zip(self._sliders, vals):
            sl.setValue(int(math.degrees(q) * 10))
        self._syncing = False
        self._on_slider(0)
