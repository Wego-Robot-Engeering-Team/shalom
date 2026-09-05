"""2D SLAM 맵 뷰.

과업지시서 2.2.7 [1] 지도 뷰 요구사항 대응:
  ① base_link 아이콘 + 방향 화살표 실시간 표시
  ② 점검포인트/Apriltag 오버레이, 상태별 색상
  ③ Nav2 계획 경로(파랑 실선) + 실제 궤적(회색 점선), Pan/Zoom
  ④ 지도 클릭 → 목표 지점 지정

RViz 의 2D Nav Goal 과 동일한 조작감을 준다: 좌클릭 드래그로 위치+방향을
한 번에 지정한다. 드래그 없이 클릭만 하면 현재 방향을 유지한다.
"""

from __future__ import annotations

import math

from PySide6.QtCore import QPointF, QRectF, Qt, Signal
from PySide6.QtGui import (
    QColor,
    QFont,
    QImage,
    QPainter,
    QPainterPath,
    QPen,
    QPixmap,
    QPolygonF,
)
from PySide6.QtWidgets import (
    QGraphicsPathItem,
    QGraphicsPixmapItem,
    QGraphicsScene,
    QGraphicsView,
)

from ..theme.tokens import mono_family, palette
from .items import AprilTagMarker, GoalMarker, RobotMarker, WaypointMarker
from .transform import MapInfo

MODE_VIEW = "view"
MODE_SET_GOAL = "goal"
MODE_ADD_WAYPOINT = "waypoint"


class MapView(QGraphicsView):
    """맵 렌더링 + 상호작용."""

    goal_requested = Signal(float, float, float)       # world x, y, theta
    waypoint_placed = Signal(float, float, float)
    waypoint_clicked = Signal(str)                     # waypoint id
    cursor_moved = Signal(float, float)                # world x, y

    MIN_SCALE = 0.15
    MAX_SCALE = 24.0

    def __init__(self, parent=None):
        super().__init__(parent)
        self._scene = QGraphicsScene(self)
        self.setScene(self._scene)

        self.setRenderHints(QPainter.Antialiasing | QPainter.SmoothPixmapTransform)
        self.setTransformationAnchor(QGraphicsView.AnchorUnderMouse)
        self.setResizeAnchor(QGraphicsView.AnchorViewCenter)
        self.setViewportUpdateMode(QGraphicsView.SmartViewportUpdate)
        self.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        self.setVerticalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        self.setFrameShape(QGraphicsView.NoFrame)
        self.setMouseTracking(True)

        self._info: MapInfo | None = None
        self._mode = MODE_VIEW

        # --- 레이어 (Z 순서: 맵 < 궤적 < 계획 < 태그 < 웨이포인트 < 목표 < 로봇)
        self._map_item = QGraphicsPixmapItem()
        self._map_item.setZValue(0)
        self._map_item.setTransformationMode(Qt.SmoothTransformation)
        self._scene.addItem(self._map_item)

        self._trail_item = QGraphicsPathItem()
        self._trail_item.setZValue(20)
        self._scene.addItem(self._trail_item)

        self._plan_item = QGraphicsPathItem()
        self._plan_item.setZValue(30)
        self._scene.addItem(self._plan_item)

        self._robot = RobotMarker()
        self._robot.setVisible(False)
        self._scene.addItem(self._robot)

        self._goal: GoalMarker | None = None
        self._waypoints: dict[str, WaypointMarker] = {}
        self._tags: dict[int, AprilTagMarker] = {}

        # 드래그로 방향 지정하는 중의 임시 상태
        self._drag_origin: QPointF | None = None
        self._drag_current: QPointF | None = None
        self._panning = False
        self._pan_anchor = QPointF()

        self.retheme()

    def retheme(self) -> None:
        """테마 전환 시 호출. 씬 아이템 펜/배경을 새 팔레트로 다시 칠한다."""
        P = palette()
        # 맵 경계 바깥은 카드 면색. 미탐색 영역 색은 맵 이미지 안에서만 쓴다.
        self.setBackgroundBrush(QColor(P.surface))
        # 실제 주행 궤적: 회색 점선 (지시서 2.2.7 [1] ③)
        self._trail_item.setPen(QPen(QColor(P.trail), 1.6, Qt.DashLine,
                                     Qt.RoundCap, Qt.RoundJoin))
        # Nav2 계획 경로: 파랑 실선
        self._plan_item.setPen(QPen(QColor(P.plan), 2.0, Qt.SolidLine,
                                    Qt.RoundCap, Qt.RoundJoin))
        self._scene.update()

    # ================= 맵 =================
    def set_map(self, info: MapInfo, image: QImage) -> None:
        self._info = info
        self._map_item.setPixmap(QPixmap.fromImage(image))
        self._map_item.setPos(0, 0)
        self._scene.setSceneRect(QRectF(0, 0, info.scene_width, info.scene_height))
        self.fit_map()

    def map_info(self) -> MapInfo | None:
        return self._info

    def fit_map(self) -> None:
        if self._info is None:
            return
        self.fitInView(self._scene.sceneRect(), Qt.KeepAspectRatio)
        self.scale(0.94, 0.94)   # 가장자리 여백

    # ================= 모드 =================
    def set_mode(self, mode: str) -> None:
        self._mode = mode
        if mode == MODE_VIEW:
            self.viewport().setCursor(Qt.ArrowCursor)
        else:
            self.viewport().setCursor(Qt.CrossCursor)

    def mode(self) -> str:
        return self._mode

    # ================= 상태 갱신 =================
    def set_robot_pose(self, x: float, y: float, theta: float, stale: bool = False) -> None:
        if self._info is None:
            return
        self._robot.setVisible(True)
        self._robot.setPos(self._info.to_scene(x, y))
        self._robot.setRotation(MapInfo.theta_to_item_rotation(theta))
        self._robot.set_stale(stale)

    def set_plan(self, pts: list[tuple[float, float]]) -> None:
        self._plan_item.setPath(self._path_from_world(pts))

    def set_trail(self, pts: list[tuple[float, float]]) -> None:
        self._trail_item.setPath(self._path_from_world(pts))

    def _path_from_world(self, pts: list[tuple[float, float]]) -> QPainterPath:
        path = QPainterPath()
        if self._info is None or len(pts) < 2:
            return path
        path.moveTo(self._info.to_scene(*pts[0]))
        for x, y in pts[1:]:
            path.lineTo(self._info.to_scene(x, y))
        return path

    def set_waypoints(self, wps: list[dict]) -> None:
        """wps: [{id, x, y, theta, status}] — 전체 치환."""
        for m in self._waypoints.values():
            self._scene.removeItem(m)
        self._waypoints.clear()
        if self._info is None:
            return
        for i, wp in enumerate(wps):
            m = WaypointMarker(i, wp["id"], wp.get("status", "todo"))
            m.setPos(self._info.to_scene(wp["x"], wp["y"]))
            self._scene.addItem(m)
            self._waypoints[wp["id"]] = m

    def set_waypoint_status(self, wp_id: str, status: str) -> None:
        m = self._waypoints.get(wp_id)
        if m is not None:
            m.set_status(status)

    def set_tags(self, tags: list[dict]) -> None:
        for m in self._tags.values():
            self._scene.removeItem(m)
        self._tags.clear()
        if self._info is None:
            return
        for t in tags:
            m = AprilTagMarker(int(t["id"]))
            m.setPos(self._info.to_scene(t["x"], t["y"]))
            self._scene.addItem(m)
            self._tags[int(t["id"])] = m

    def set_tags_seen(self, seen_ids: set[int]) -> None:
        for tid, m in self._tags.items():
            m.set_seen(tid in seen_ids)

    def set_goal(self, x: float, y: float, theta: float) -> None:
        if self._info is None:
            return
        if self._goal is None:
            self._goal = GoalMarker()
            self._scene.addItem(self._goal)
        self._goal.setVisible(True)
        self._goal.setPos(self._info.to_scene(x, y))
        self._goal.setRotation(MapInfo.theta_to_item_rotation(theta))

    def clear_goal(self) -> None:
        if self._goal is not None:
            self._goal.setVisible(False)

    # ================= 입력 =================
    def wheelEvent(self, ev) -> None:
        if self._info is None:
            return
        factor = 1.18 if ev.angleDelta().y() > 0 else 1 / 1.18
        cur = self.transform().m11()
        target = cur * factor
        if target < self.MIN_SCALE or target > self.MAX_SCALE:
            return
        self.scale(factor, factor)

    def mousePressEvent(self, ev) -> None:
        # 중클릭 또는 우클릭 = 패닝. 좌클릭은 모드에 따라 동작.
        if ev.button() in (Qt.MiddleButton, Qt.RightButton):
            self._panning = True
            self._pan_anchor = ev.position()
            self.viewport().setCursor(Qt.ClosedHandCursor)
            return

        if ev.button() == Qt.LeftButton:
            if self._mode == MODE_VIEW:
                item = self.itemAt(ev.position().toPoint())
                if isinstance(item, WaypointMarker):
                    self.waypoint_clicked.emit(item.waypoint_id())
                return
            # 목표/웨이포인트 지정: 드래그로 방향까지 잡는다
            self._drag_origin = self.mapToScene(ev.position().toPoint())
            self._drag_current = self._drag_origin
            self.viewport().update()

    def mouseMoveEvent(self, ev) -> None:
        if self._panning:
            delta = ev.position() - self._pan_anchor
            self._pan_anchor = ev.position()
            self.horizontalScrollBar().setValue(
                self.horizontalScrollBar().value() - int(delta.x()))
            self.verticalScrollBar().setValue(
                self.verticalScrollBar().value() - int(delta.y()))
            return

        sp = self.mapToScene(ev.position().toPoint())
        if self._info is not None:
            wx, wy = self._info.to_world(sp.x(), sp.y())
            self.cursor_moved.emit(wx, wy)

        if self._drag_origin is not None:
            self._drag_current = sp
            self.viewport().update()

    def mouseReleaseEvent(self, ev) -> None:
        if self._panning and ev.button() in (Qt.MiddleButton, Qt.RightButton):
            self._panning = False
            self.viewport().setCursor(
                Qt.ArrowCursor if self._mode == MODE_VIEW else Qt.CrossCursor)
            return

        if ev.button() == Qt.LeftButton and self._drag_origin is not None and self._info:
            origin = self._drag_origin
            end = self.mapToScene(ev.position().toPoint())
            self._drag_origin = None
            self._drag_current = None
            self.viewport().update()

            wx, wy = self._info.to_world(origin.x(), origin.y())
            dx, dy = end.x() - origin.x(), end.y() - origin.y()
            # 드래그 거리가 짧으면 방향 미지정으로 보고 0 을 준다.
            if math.hypot(dx, dy) < 8.0:
                theta = 0.0
            else:
                # 씬 y 가 아래로 향하므로 부호를 뒤집어 world 각으로 변환
                theta = math.atan2(-dy, dx)

            if self._mode == MODE_SET_GOAL:
                self.set_goal(wx, wy, theta)
                self.goal_requested.emit(wx, wy, theta)
            elif self._mode == MODE_ADD_WAYPOINT:
                self.waypoint_placed.emit(wx, wy, theta)
            self.set_mode(MODE_VIEW)

    # ================= 오버레이 (스케일바 / 드래그 프리뷰) =================
    def drawForeground(self, p: QPainter, _rect: QRectF) -> None:
        p.save()
        p.resetTransform()          # 화면 좌표로 그린다
        p.setRenderHint(QPainter.Antialiasing)

        if self._drag_origin is not None and self._drag_current is not None:
            o = self.mapFromScene(self._drag_origin)
            c = self.mapFromScene(self._drag_current)
            col = QColor(palette().accent)
            p.setPen(QPen(col, 1.6, Qt.DashLine))
            p.drawLine(o, c)
            p.setPen(Qt.NoPen); p.setBrush(col)
            p.drawEllipse(QPointF(o), 4, 4)
            d = math.hypot(c.x() - o.x(), c.y() - o.y())
            if d > 8:
                a = math.atan2(c.y() - o.y(), c.x() - o.x())
                p.drawPolygon(QPolygonF([
                    QPointF(c.x(), c.y()),
                    QPointF(c.x() - 11 * math.cos(a - 0.4), c.y() - 11 * math.sin(a - 0.4)),
                    QPointF(c.x() - 11 * math.cos(a + 0.4), c.y() - 11 * math.sin(a + 0.4)),
                ]))

        self._draw_scalebar(p)
        p.restore()

    def _draw_scalebar(self, p: QPainter) -> None:
        if self._info is None:
            return
        px_per_m = self.transform().m11() / self._info.resolution
        # 화면에서 60~150px 사이가 되는 '깔끔한' 미터 값을 고른다
        target_px = 110.0
        raw = target_px / px_per_m
        nice = min([0.1, 0.2, 0.5, 1, 2, 5, 10, 20, 50, 100],
                   key=lambda v: abs(math.log10(v / raw)) if raw > 0 else 0)
        length_px = nice * px_per_m
        if not (20 < length_px < 400):
            return

        P = palette()
        x0, y0 = 14, self.viewport().height() - 18
        p.setPen(QPen(QColor(P.text_mute), 1.4))
        p.drawLine(x0, y0, x0 + length_px, y0)
        p.drawLine(x0, y0 - 3, x0, y0 + 3)
        p.drawLine(x0 + length_px, y0 - 3, x0 + length_px, y0 + 3)

        f = QFont(mono_family()); f.setPointSize(9)
        p.setFont(f)
        p.setPen(QColor(P.text_mute))
        label = f"{nice:g} m" if nice >= 1 else f"{nice * 100:g} cm"
        p.drawText(QRectF(x0, y0 - 19, length_px, 14), Qt.AlignCenter, label)
