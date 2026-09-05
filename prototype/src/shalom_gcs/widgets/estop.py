"""E-Stop 버튼과 발동 상태 표시.

과업지시서 2.2.7 [5]: 상시 표시, 1회 클릭 즉시 발동,
발동 시 화면 전체 빨간 경고 테두리.

형태는 실제 산업용 비상정지 스위치(적색 버섯형 버튼 + 황색 베이스)를 따른다.
소프트웨어 장식이 아니라 하드웨어 조작계의 은유로 읽혀야 조작자가 망설이지 않는다.

⚠️ 이 위젯은 요청자이지 판정자가 아니다. 1초 이내 정지 보장은 로봇측 safety
노드의 SDK2 E-Stop API + /cmd_vel 차단이 담당하며, 주 수단은 물리/무선
하드웨어 E-Stop 이어야 한다. bridge_protocol.md §4 참조.
"""

from __future__ import annotations

from PySide6.QtCore import (
    Property,
    QEasingCurve,
    QPropertyAnimation,
    QRectF,
    Qt,
    Signal,
)
from PySide6.QtGui import QColor, QFont, QPainter, QPen
from PySide6.QtWidgets import QWidget

from ..theme.tokens import palette


class EStopButton(QWidget):
    engage_requested = Signal()
    release_requested = Signal()

    def __init__(self, parent: QWidget | None = None, *, size: int = 64):
        super().__init__(parent)
        self._size = size
        self._engaged = False
        self._hover = False
        self._pulse = 0.0
        self.setFixedSize(size, size)
        self.setCursor(Qt.PointingHandCursor)
        self.setToolTip("비상정지 — 1회 클릭으로 즉시 발동")

        self._anim = QPropertyAnimation(self, b"pulse", self)
        self._anim.setDuration(760)
        self._anim.setStartValue(0.0)
        self._anim.setEndValue(1.0)
        self._anim.setEasingCurve(QEasingCurve.InOutSine)
        self._anim.setLoopCount(-1)

    def get_pulse(self) -> float:
        return self._pulse

    def set_pulse(self, v: float) -> None:
        self._pulse = v
        self.update()

    pulse = Property(float, get_pulse, set_pulse)

    def set_engaged(self, engaged: bool) -> None:
        if engaged == self._engaged:
            return
        self._engaged = engaged
        if engaged:
            self._anim.start()
        else:
            self._anim.stop()
            self._pulse = 0.0
        self.update()

    def is_engaged(self) -> bool:
        return self._engaged

    def enterEvent(self, ev):
        self._hover = True; self.update(); super().enterEvent(ev)

    def leaveEvent(self, ev):
        self._hover = False; self.update(); super().leaveEvent(ev)

    def mousePressEvent(self, ev):
        if ev.button() != Qt.LeftButton:
            return
        # 발동은 확인 없이 즉시. 해제만 상위에서 확인 절차를 태운다.
        (self.release_requested if self._engaged else self.engage_requested).emit()

    def paintEvent(self, _ev) -> None:
        P = palette()
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)

        cx, cy = self.width() / 2, self.height() / 2
        outer_r = self._size * 0.46
        btn_r = self._size * 0.34

        # 베이스 링 — 산업용 스위치의 황색 베이스 대신 중립 테두리로 절제
        p.setPen(QPen(QColor(P.border_hi), 1))
        p.setBrush(QColor(P.surface_hi))
        p.drawEllipse(QRectF(cx - outer_r, cy - outer_r, outer_r * 2, outer_r * 2))

        # 발동 시에만 얇은 링을 맥동시킨다 (글로우 대신)
        if self._engaged:
            ring = QColor(P.danger)
            ring.setAlpha(int(90 + 130 * self._pulse))
            p.setPen(QPen(ring, 2.0))
            p.setBrush(Qt.NoBrush)
            rr = outer_r + 1 + 2 * self._pulse
            p.drawEllipse(QRectF(cx - rr, cy - rr, rr * 2, rr * 2))

        # 버튼 본체
        body = QColor(P.danger_hi if (self._hover or self._engaged) else P.danger)
        p.setPen(QPen(QColor(P.danger_lo), 1.5))
        p.setBrush(body)
        p.drawEllipse(QRectF(cx - btn_r, cy - btn_r, btn_r * 2, btn_r * 2))

        f = QFont(); f.setPointSize(max(7, int(self._size * 0.115)))
        f.setWeight(QFont.Bold)
        p.setFont(f)
        p.setPen(QColor("#FFFFFF"))
        p.drawText(QRectF(cx - btn_r, cy - btn_r, btn_r * 2, btn_r * 2),
                   Qt.AlignCenter, "STOP")
        p.end()


class AlertFrame(QWidget):
    """E-Stop 발동 시 화면 전체를 감싸는 경고 테두리.

    마우스 이벤트는 통과시켜 아래 위젯 조작을 막지 않는다.
    """

    def __init__(self, parent: QWidget | None = None):
        super().__init__(parent)
        self.setAttribute(Qt.WA_TransparentForMouseEvents)
        self._active = False
        self._pulse = 0.0
        self.hide()

        self._anim = QPropertyAnimation(self, b"pulse", self)
        self._anim.setDuration(1000)
        self._anim.setStartValue(0.0)
        self._anim.setEndValue(1.0)
        self._anim.setEasingCurve(QEasingCurve.InOutSine)
        self._anim.setLoopCount(-1)

    def get_pulse(self) -> float:
        return self._pulse

    def set_pulse(self, v: float) -> None:
        self._pulse = v
        self.update()

    pulse = Property(float, get_pulse, set_pulse)

    def set_active(self, active: bool) -> None:
        if active == self._active:
            return
        self._active = active
        if active:
            self.show(); self.raise_(); self._anim.start()
        else:
            self._anim.stop(); self.hide()

    def paintEvent(self, _ev) -> None:
        if not self._active:
            return
        P = palette()
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        width = 3.0
        c = QColor(P.danger)
        c.setAlpha(int(140 + 90 * self._pulse))
        p.setPen(QPen(c, width))
        p.setBrush(Qt.NoBrush)
        i = width / 2
        p.drawRect(QRectF(i, i, self.width() - width, self.height() - width))
        p.end()
