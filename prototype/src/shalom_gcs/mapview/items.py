"""지도 위에 얹는 QGraphicsItem 들.

전부 씬 좌표(픽셀=셀)로 배치되지만, 아이콘 자체는 줌 배율과 무관하게
일정한 화면 크기를 유지해야 한다(ItemIgnoresTransformations).
그러지 않으면 축소했을 때 로봇 아이콘이 점으로 사라진다.
"""

from __future__ import annotations

from ..theme.tokens import mono_family, palette

from PySide6.QtCore import QPointF, QRectF, Qt
from PySide6.QtGui import QColor, QFont, QPainter, QPen, QPolygonF
from PySide6.QtWidgets import QGraphicsItem


def wp_color(status: str) -> str:
    """과업지시서 2.2.7 [1] ② 규정 상태색. 의미 대응을 바꾸지 말 것."""
    P = palette()
    return {
        "done": P.wp_done,
        "current": P.wp_current,
        "todo": P.wp_todo,
        "error": P.wp_error,
    }.get(status, P.wp_todo)


class RobotMarker(QGraphicsItem):
    """base_link 위치 + 방향 화살표.

    지시서 요구: "로봇 현재 위치(base_link) 및 방향을 아이콘+화살표로 실시간 표시"
    """

    def __init__(self, radius: float = 9.0):
        super().__init__()
        self._r = radius
        self._stale = False
        self.setFlag(QGraphicsItem.ItemIgnoresTransformations, True)
        self.setZValue(100)

    def set_stale(self, stale: bool) -> None:
        """통신 두절 등으로 위치가 오래된 상태. 흐리게 표시한다."""
        if stale != self._stale:
            self._stale = stale
            self.update()

    def boundingRect(self) -> QRectF:
        e = self._r * 3.2
        return QRectF(-e, -e, e * 2, e * 2)

    def paint(self, p: QPainter, _opt, _w=None) -> None:
        P = palette()
        p.setRenderHint(QPainter.Antialiasing)
        col = QColor(P.text_mute) if self._stale else QColor(P.accent)

        # 진행 방향 지시선 — 로컬 +x
        p.setPen(QPen(col, 1.5))
        p.drawLine(QPointF(self._r, 0), QPointF(self._r * 2.1, 0))

        p.setPen(QPen(QColor(P.surface), 2.0))
        p.setBrush(col)
        p.drawEllipse(QPointF(0, 0), self._r, self._r)

        p.setPen(Qt.NoPen)
        p.setBrush(QColor(P.text_on_accent))
        p.drawPolygon(QPolygonF([
            QPointF(self._r * 0.72, 0.0),
            QPointF(-self._r * 0.24, -self._r * 0.46),
            QPointF(-self._r * 0.24, self._r * 0.46),
        ]))


class WaypointMarker(QGraphicsItem):
    """점검포인트. 상태별 색상 + 순번 라벨."""

    def __init__(self, index: int, wp_id: str, status: str = "todo", radius: float = 10.0):
        super().__init__()
        self._index = index
        self._id = wp_id
        self._status = status
        self._r = radius
        self._hover = False
        self.setFlag(QGraphicsItem.ItemIgnoresTransformations, True)
        self.setAcceptHoverEvents(True)
        self.setZValue(60)
        self.setToolTip(f"{wp_id}  (#{index + 1})")

    def set_status(self, status: str) -> None:
        if status != self._status:
            self._status = status
            self.setZValue(80 if status == "current" else 60)
            self.update()

    def status(self) -> str:
        return self._status

    def waypoint_id(self) -> str:
        return self._id

    def hoverEnterEvent(self, ev):
        self._hover = True; self.update(); super().hoverEnterEvent(ev)

    def hoverLeaveEvent(self, ev):
        self._hover = False; self.update(); super().hoverLeaveEvent(ev)

    def boundingRect(self) -> QRectF:
        e = self._r * 2.4
        return QRectF(-e, -e, e * 2, e * 2)

    def paint(self, p: QPainter, _opt, _w=None) -> None:
        P = palette()
        p.setRenderHint(QPainter.Antialiasing)
        col = QColor(wp_color(self._status))
        r = self._r * (1.12 if self._hover else 1.0)

        if self._status == "current":
            ring = QColor(col); ring.setAlpha(60)
            p.setPen(Qt.NoPen); p.setBrush(ring)
            p.drawEllipse(QPointF(0, 0), r * 1.9, r * 1.9)

        filled = self._status != "todo"
        p.setPen(QPen(QColor(P.surface), 1.5) if filled else QPen(col, 1.5))
        p.setBrush(col if filled else QColor(P.surface))
        p.drawEllipse(QPointF(0, 0), r, r)

        f = QFont(); f.setPointSize(8); f.setWeight(QFont.DemiBold)
        p.setFont(f)
        p.setPen(QColor(P.text_on_accent) if filled else QColor(P.text_dim))
        p.drawText(QRectF(-r, -r, r * 2, r * 2), Qt.AlignCenter, str(self._index + 1))


class AprilTagMarker(QGraphicsItem):
    """Apriltag 마커 위치. 사각형 + ID."""

    def __init__(self, tag_id: int, size: float = 9.0):
        super().__init__()
        self._id = tag_id
        self._s = size
        self._seen = False
        self.setFlag(QGraphicsItem.ItemIgnoresTransformations, True)
        self.setZValue(50)
        self.setToolTip(f"AprilTag #{tag_id}")

    def set_seen(self, seen: bool) -> None:
        if seen != self._seen:
            self._seen = seen
            self.update()

    def boundingRect(self) -> QRectF:
        e = self._s * 2.2
        return QRectF(-e, -e, e * 2, e * 2)

    def paint(self, p: QPainter, _opt, _w=None) -> None:
        P = palette()
        p.setRenderHint(QPainter.Antialiasing)
        col = QColor(P.tag)
        if self._seen:
            halo = QColor(col); halo.setAlpha(70)
            p.setPen(Qt.NoPen); p.setBrush(halo)
            p.drawRoundedRect(QRectF(-self._s * 1.7, -self._s * 1.7,
                                     self._s * 3.4, self._s * 3.4), 3, 3)

        p.setPen(QPen(col, 1.5))
        p.setBrush(col if self._seen else QColor(P.surface))
        p.drawRect(QRectF(-self._s, -self._s, self._s * 2, self._s * 2))

        f = QFont(); f.setPointSize(7); f.setWeight(QFont.DemiBold)
        p.setFont(f)
        p.setPen(QColor(P.text_on_accent) if self._seen else col)
        p.drawText(QRectF(-self._s, -self._s, self._s * 2, self._s * 2),
                   Qt.AlignCenter, str(self._id))


class GoalMarker(QGraphicsItem):
    """지도 클릭으로 지정한 목표점 + 목표 방향."""

    def __init__(self, radius: float = 9.0):
        super().__init__()
        self._r = radius
        self.setFlag(QGraphicsItem.ItemIgnoresTransformations, True)
        self.setZValue(95)

    def boundingRect(self) -> QRectF:
        e = self._r * 3.0
        return QRectF(-e, -e, e * 2, e * 2)

    def paint(self, p: QPainter, _opt, _w=None) -> None:
        P = palette()
        p.setRenderHint(QPainter.Antialiasing)
        col = QColor(P.accent)

        p.setPen(QPen(col, 1.5, Qt.DashLine))
        p.setBrush(Qt.NoBrush)
        p.drawEllipse(QPointF(0, 0), self._r * 1.7, self._r * 1.7)

        p.setPen(Qt.NoPen)
        p.setBrush(col)
        p.drawEllipse(QPointF(0, 0), self._r * 0.32, self._r * 0.32)

        # 목표 방향 화살표 (로컬 +x)
        p.setPen(QPen(col, 1.8))
        p.drawLine(QPointF(0, 0), QPointF(self._r * 2.4, 0))
        p.setPen(Qt.NoPen)
        p.drawPolygon(QPolygonF([
            QPointF(self._r * 3.0, 0.0),
            QPointF(self._r * 2.1, -self._r * 0.5),
            QPointF(self._r * 2.1, self._r * 0.5),
        ]))
