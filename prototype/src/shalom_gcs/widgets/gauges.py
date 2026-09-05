"""QPainter 로 직접 그리는 계측 위젯.

기본 위젯(QProgressBar 등)에 QSS 를 발라도 계측기처럼 보이지 않는다.
반면 안티에일리어싱 호/막대는 QPainter 로 짧게 나오고, 이게 화면 인상의
대부분을 만든다. 웹 대비 Qt 가 유리한 지점.

값 변화는 QPropertyAnimation 으로 이징한다. 다만 짧게(280ms) 둔다 —
관제 화면에서 애니메이션이 길면 '지금 값'을 못 믿게 된다.

색상은 paint 시점에 palette() 를 조회한다. 재시작 없이 테마가 바뀌게 하기 위함.
"""

from __future__ import annotations

import math

from PySide6.QtCore import Property, QEasingCurve, QPropertyAnimation, QRectF, Qt
from PySide6.QtGui import QColor, QFont, QPainter, QPen
from PySide6.QtWidgets import QSizePolicy, QWidget

from ..theme.tokens import mono_family, palette


class _Animated(QWidget):
    """부드럽게 따라가는 스칼라 값 하나를 갖는 베이스."""

    def __init__(self, parent: QWidget | None = None, *, duration: int = 280):
        super().__init__(parent)
        self._value = 0.0
        self._anim = QPropertyAnimation(self, b"value", self)
        self._anim.setDuration(duration)
        self._anim.setEasingCurve(QEasingCurve.OutCubic)

    def get_value(self) -> float:
        return self._value

    def set_value(self, v: float) -> None:
        self._value = float(v)
        self.update()

    value = Property(float, get_value, set_value)

    def animate_to(self, target: float) -> None:
        if abs(target - self._value) < 1e-4:
            return
        self._anim.stop()
        self._anim.setStartValue(self._value)
        self._anim.setEndValue(float(target))
        self._anim.start()


def _mono(size: int, bold: bool = False) -> QFont:
    f = QFont(mono_family())
    f.setPointSize(size)
    if bold:
        f.setWeight(QFont.DemiBold)
    return f


class BatteryRing(_Animated):
    """배터리 잔량 원형 게이지.

    시나리오 예외처리에 '최소 배터리 용량 미만 시 즉시 충전 복귀'가 있어
    임계값 위치를 눈금으로 명시한다.
    """

    def __init__(self, parent: QWidget | None = None, *, size: int = 86,
                 low_threshold: float = 25.0):
        super().__init__(parent)
        self._size = size
        self._low = low_threshold
        self._charging = False
        self.setFixedSize(size, size)

    def set_state(self, soc_pct: float, charging: bool = False) -> None:
        self._charging = charging
        self.animate_to(max(0.0, min(100.0, soc_pct)))

    def _arc_color(self) -> QColor:
        P = palette()
        if self._charging:
            return QColor(P.accent)
        if self._value <= self._low * 0.6:
            return QColor(P.danger)
        if self._value <= self._low:
            return QColor(P.warning)
        return QColor(P.success)

    def paintEvent(self, _ev) -> None:
        P = palette()
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)

        thickness = 6.0
        pad = thickness / 2 + 1
        rect = QRectF(pad, pad, self.width() - pad * 2, self.height() - pad * 2)

        p.setPen(QPen(QColor(P.surface_hi if P.is_dark else P.surface_hover),
                      thickness, Qt.SolidLine, Qt.FlatCap))
        p.drawArc(rect, 0, 360 * 16)

        if self._value > 0.3:
            p.setPen(QPen(self._arc_color(), thickness, Qt.SolidLine, Qt.FlatCap))
            p.drawArc(rect, 90 * 16, -int(360 * 16 * self._value / 100.0))

        # 임계값 눈금 — 배경색으로 호를 끊어 표시
        ang = math.radians(90 - 360 * self._low / 100.0)
        cx, cy = self.width() / 2, self.height() / 2
        r_out = rect.width() / 2 + thickness / 2
        r_in = rect.width() / 2 - thickness / 2
        p.setPen(QPen(QColor(P.surface), 2))
        p.drawLine(cx + r_in * math.cos(ang), cy - r_in * math.sin(ang),
                   cx + r_out * math.cos(ang), cy - r_out * math.sin(ang))

        p.setFont(_mono(int(self._size * 0.21), bold=True))
        p.setPen(QColor(P.text))
        p.drawText(self.rect().adjusted(0, -5, 0, -5), Qt.AlignCenter, f"{self._value:.0f}")

        f2 = QFont(); f2.setPointSize(max(7, int(self._size * 0.09)))
        p.setFont(f2)
        p.setPen(QColor(P.text_mute))
        off = int(self._size * 0.27)
        p.drawText(self.rect().adjusted(0, off, 0, off), Qt.AlignCenter,
                   "충전 중" if self._charging else "%")
        p.end()


class ArcGauge(_Animated):
    """반원 호 게이지 — FR3 조작성 지수(특이자세 근접도).

    낮을수록 위험한 지표라 색이 역방향이다. 실제 특이자세 판정·회피는
    로봇측 암 노드 책임이고 여기선 표시만 한다 (bridge_protocol.md §4).
    """

    def __init__(self, parent: QWidget | None = None, *, caption: str = "",
                 warn_below: float = 0.35, danger_below: float = 0.15):
        super().__init__(parent)
        self._caption = caption
        self._warn = warn_below
        self._danger = danger_below
        self._raw_text = "—"
        self.setMinimumHeight(70)
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)

    def set_state(self, normalized: float, raw_text: str = "") -> None:
        self._raw_text = raw_text or f"{normalized:.3f}"
        self.animate_to(max(0.0, min(1.0, normalized)))

    def tone_color(self) -> QColor:
        P = palette()
        if self._value <= self._danger:
            return QColor(P.danger)
        if self._value <= self._warn:
            return QColor(P.warning)
        return QColor(P.success)

    def paintEvent(self, _ev) -> None:
        P = palette()
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)

        thickness = 6.0
        w, h = self.width(), self.height()
        d = min(w - 14, (h - 22) * 2)
        rect = QRectF((w - d) / 2, 9, d, d)

        p.setPen(QPen(QColor(P.surface_hi if P.is_dark else P.surface_hover),
                      thickness, Qt.SolidLine, Qt.FlatCap))
        p.drawArc(rect, 180 * 16, -180 * 16)

        p.setPen(QPen(self.tone_color(), thickness, Qt.SolidLine, Qt.FlatCap))
        p.drawArc(rect, 180 * 16, -int(180 * 16 * self._value))

        p.setFont(_mono(12, bold=True))
        p.setPen(QColor(P.text))
        p.drawText(QRectF(0, h - 32, w, 18), Qt.AlignCenter, self._raw_text)

        if self._caption:
            f2 = QFont(); f2.setPointSize(8)
            p.setFont(f2)
            p.setPen(QColor(P.text_mute))
            p.drawText(QRectF(0, h - 15, w, 13), Qt.AlignCenter, self._caption)
        p.end()


class StatBar(QWidget):
    """'라벨 ─ 값' 한 줄 막대. CPU/GPU 온도, 사용률, 링크 지연."""

    def __init__(self, label: str, unit: str = "", parent: QWidget | None = None,
                 *, warn_above: float | None = None, danger_above: float | None = None,
                 vmax: float = 100.0):
        super().__init__(parent)
        self._label = label
        self._unit = unit
        self._warn = warn_above
        self._danger = danger_above
        self._vmax = vmax
        self._value = 0.0
        self._valid = False
        self.setFixedHeight(26)

    def set_value(self, v: float | None) -> None:
        self._valid = v is not None
        self._value = float(v or 0.0)
        self.update()

    def _color(self) -> QColor:
        P = palette()
        if not self._valid:
            return QColor(P.text_mute)
        if self._danger is not None and self._value >= self._danger:
            return QColor(P.danger)
        if self._warn is not None and self._value >= self._warn:
            return QColor(P.warning)
        return QColor(P.text_dim)

    def paintEvent(self, _ev) -> None:
        P = palette()
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        w = self.width()

        f = QFont(); f.setPointSize(9)
        p.setFont(f)
        p.setPen(QColor(P.text_mute))
        p.drawText(QRectF(0, 0, w * 0.55, 13), Qt.AlignLeft | Qt.AlignVCenter, self._label)

        p.setFont(_mono(9))
        p.setPen(QColor(P.text) if self._valid else QColor(P.text_mute))
        txt = f"{self._value:.0f}{self._unit}" if self._valid else "—"
        p.drawText(QRectF(w * 0.45, 0, w * 0.55, 13), Qt.AlignRight | Qt.AlignVCenter, txt)

        p.setPen(Qt.NoPen)
        p.setBrush(QColor(P.surface_hi if P.is_dark else P.surface_hover))
        p.drawRect(QRectF(0, 18, w, 3))

        if self._valid and self._value > 0:
            frac = max(0.0, min(1.0, self._value / self._vmax))
            p.setBrush(self._color())
            p.drawRect(QRectF(0, 18, max(2.0, w * frac), 3))
        p.end()


class JointBar(QWidget):
    """관절 하나의 실제값을 한계 범위 안에서 표시.

    조작은 QSlider 가 하고 이건 표시 전용이다. 명령값과 실제값이 갈라지는
    상황(계획 중, 리플렉스 중단)을 조작자가 봐야 하므로 둘을 함께 그린다.
    """

    def __init__(self, name: str, lo: float, hi: float, parent: QWidget | None = None):
        super().__init__(parent)
        self._name = name
        self._lo, self._hi = lo, hi
        self._actual = 0.0
        self._command: float | None = None
        self.setFixedHeight(22)

    def set_actual(self, rad: float) -> None:
        self._actual = rad
        self.update()

    def set_command(self, rad: float | None) -> None:
        self._command = rad
        self.update()

    def _frac(self, v: float) -> float:
        return max(0.0, min(1.0, (v - self._lo) / (self._hi - self._lo)))

    def _near_limit(self) -> bool:
        f = self._frac(self._actual)
        return f < 0.04 or f > 0.96

    def paintEvent(self, _ev) -> None:
        P = palette()
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        w, h = self.width(), self.height()
        bar_x = 26
        bar_w = w - bar_x - 56

        f = QFont(); f.setPointSize(9)
        p.setFont(f)
        p.setPen(QColor(P.text_mute))
        p.drawText(QRectF(0, 0, 22, h), Qt.AlignLeft | Qt.AlignVCenter, self._name)

        y = h / 2 - 1.5
        p.setPen(Qt.NoPen)
        p.setBrush(QColor(P.surface_hi if P.is_dark else P.surface_hover))
        p.drawRect(QRectF(bar_x, y, bar_w, 3))

        # 명령값 고스트
        if self._command is not None:
            cx = bar_x + bar_w * self._frac(self._command)
            p.setBrush(QColor(P.text_mute))
            p.drawRect(QRectF(cx - 1, y - 3, 2, 9))

        # 실제값. 한계 근접(양 끝 4%)은 마커 색으로만 알린다 —
        # 막대에 음영을 깔면 7줄이 겹쳐 얼룩처럼 보인다.
        ax = bar_x + bar_w * self._frac(self._actual)
        p.setBrush(QColor(P.warning) if self._near_limit() else QColor(P.accent))
        p.drawEllipse(QRectF(ax - 3.5, h / 2 - 3.5, 7, 7))

        p.setFont(_mono(9))
        p.setPen(QColor(P.text))
        p.drawText(QRectF(w - 52, 0, 52, h), Qt.AlignRight | Qt.AlignVCenter,
                   f"{math.degrees(self._actual):+.1f}°")
        p.end()
