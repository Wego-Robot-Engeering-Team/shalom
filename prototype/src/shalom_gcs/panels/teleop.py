"""수동 조이스틱 패널 — 과업지시서 2.2.7 [2] ②.

방향 버튼 + 선속도·각속도 슬라이더. /cmd_vel 을 20 Hz 로 발행한다.

안전 설계:
  - 버튼을 누르고 있는 동안에만 발행한다(press/release). 토글이 아니다.
  - 발행이 멈추면 브릿지가 300ms 데드맨으로 0 을 래치한다.
    즉 UI 가 죽어도 로봇이 계속 달리지 않는다.
  - 수동 모드가 아니면 조작 자체가 비활성화된다.
"""

from __future__ import annotations

from PySide6.QtCore import Qt, QTimer, Signal
from PySide6.QtWidgets import (
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QSlider,
    QVBoxLayout,
    QWidget,
)

from ..robot_def import B2_VX_CAUTION, B2_VX_MAX, B2_WZ_MAX
from ..theme.tokens import METRICS as M
from ..widgets.primitives import Card, readout, section_label

PUBLISH_HZ = 20


class TeleopPanel(Card):
    cmd_vel = Signal(float, float, float)   # vx, vy, wz

    def __init__(self, parent: QWidget | None = None):
        super().__init__("수동 조작", parent)

        self._vx = self._vy = self._wz = 0.0
        self._enabled = False

        # ---- 방향 버튼 십자 배열 ----
        grid = QGridLayout()
        grid.setSpacing(M.s1)
        self._btns: dict[str, QPushButton] = {}
        layout_map = {
            "rot_l": (0, 0, "↺"), "fwd": (0, 1, "▲"), "rot_r": (0, 2, "↻"),
            "left":  (1, 0, "◀"), "stop": (1, 1, "■"), "right": (1, 2, "▶"),
            "back":  (2, 1, "▼"),
        }
        for key, (r, c, txt) in layout_map.items():
            b = QPushButton(txt)
            b.setFixedSize(46, 34)
            b.setAutoRepeat(False)
            if key == "stop":
                b.setProperty("variant", "danger")
                b.clicked.connect(self._stop)
            else:
                b.pressed.connect(lambda k=key: self._press(k))
                b.released.connect(self._release)
            grid.addWidget(b, r, c)
            self._btns[key] = b
        holder = QHBoxLayout()
        holder.addStretch(1); holder.addLayout(grid); holder.addStretch(1)
        self.body.addLayout(holder)

        # ---- 속도 슬라이더 ----
        self.body.addSpacing(M.s2)
        self.sl_lin, self.lbl_lin = self._speed_row(
            "선속도", B2_VX_MAX, 0.30, "m/s", caution=B2_VX_CAUTION)
        self.sl_ang, self.lbl_ang = self._speed_row(
            "각속도", B2_WZ_MAX, 0.50, "rad/s")

        note = QLabel(f"버튼을 누르는 동안만 발행 · {PUBLISH_HZ} Hz · 데드맨 300 ms")
        note.setObjectName("Hint")
        note.setWordWrap(True)
        self.body.addWidget(note)
        self.body.addStretch(1)

        self._timer = QTimer(self)
        self._timer.setInterval(int(1000 / PUBLISH_HZ))
        self._timer.timeout.connect(self._tick)

        self.set_enabled(False)

    def _speed_row(self, label: str, vmax: float, default: float, unit: str,
                   caution: float | None = None):
        host = QWidget()
        lay = QVBoxLayout(host)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(M.s1)

        head = QHBoxLayout()
        head.addWidget(section_label(label))
        head.addStretch(1)
        val = readout(f"{default:.2f} {unit}")
        head.addWidget(val)
        lay.addLayout(head)

        sl = QSlider(Qt.Horizontal)
        sl.setRange(5, int(vmax * 100))
        sl.setValue(int(default * 100))

        def on_change(v: int) -> None:
            val.setText(f"{v / 100:.2f} {unit}")
            # 감속 기준선 초과 시 경고색 (지시서 2.2.5: 미등록 물체 접근 시 30cm/s)
            if caution is not None:
                warn = "true" if v / 100 > caution else "false"
                if sl.property("warn") != warn:
                    sl.setProperty("warn", warn)
                    sl.style().unpolish(sl); sl.style().polish(sl)

        sl.valueChanged.connect(on_change)
        on_change(sl.value())
        lay.addWidget(sl)
        self.body.addWidget(host)
        return sl, val

    # ---- 조작 ----
    def set_enabled(self, on: bool) -> None:
        self._enabled = on
        for b in self._btns.values():
            b.setEnabled(on or b is self._btns["stop"])
        self.sl_lin.setEnabled(on)
        self.sl_ang.setEnabled(on)
        if not on:
            self._stop()

    def _press(self, key: str) -> None:
        if not self._enabled:
            return
        lin = self.sl_lin.value() / 100.0
        ang = self.sl_ang.value() / 100.0
        self._vx = {"fwd": lin, "back": -lin}.get(key, 0.0)
        self._vy = {"left": lin * 0.7, "right": -lin * 0.7}.get(key, 0.0)
        self._wz = {"rot_l": ang, "rot_r": -ang}.get(key, 0.0)
        if not self._timer.isActive():
            self._timer.start()
        self._tick()

    def _release(self) -> None:
        self._vx = self._vy = self._wz = 0.0
        self._tick()               # 즉시 0 한 번
        self._timer.stop()

    def _stop(self) -> None:
        self._release()
        self.cmd_vel.emit(0.0, 0.0, 0.0)

    def _tick(self) -> None:
        self.cmd_vel.emit(self._vx, self._vy, self._wz)
