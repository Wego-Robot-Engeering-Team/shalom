"""관제 메인 윈도우 — 전체 레이아웃 조립.

  ┌─ TopBar: 타이틀 · 연결/모드 배지 · 모드 전환 · E-Stop ────────────┐
  ├──────────┬───────────────────────────────┬──────────────────────┤
  │ 상태     │  지도 뷰 (+ 오버레이 툴바)    │  점검포인트 시퀀스   │
  │ 수동조작 │                               │  로봇팔 제어         │
  │ 이벤트   │                               │                      │
  └──────────┴───────────────────────────────┴──────────────────────┘
"""

from __future__ import annotations

import math

from PySide6.QtCore import Qt, QTimer
from PySide6.QtWidgets import (
    QApplication,
    QHBoxLayout,
    QLabel,
    QMainWindow,
    QMessageBox,
    QPushButton,
    QScrollArea,
    QSplitter,
    QVBoxLayout,
    QWidget,
)

from . import demo
from .mapview.render import occupancy_to_image
from .mapview.view import MODE_ADD_WAYPOINT, MODE_SET_GOAL, MODE_VIEW, MapView
from .panels.arm import ArmPanel
from .panels.eventlog import EventLogPanel
from .panels.status import StatusPanel
from .panels.teleop import TeleopPanel
from .panels.waypoints import WaypointPanel
from .theme.style import build_qss
from .theme.tokens import METRICS as M
from .theme.tokens import set_theme, toggle_theme
from .theme.tokens import mono_family, palette
from .widgets.estop import AlertFrame, EStopButton
from .widgets.primitives import Badge, Card


class MapCard(QWidget):
    """지도 뷰 + 위에 떠 있는 툴바/리드아웃."""

    def __init__(self, parent: QWidget | None = None):
        super().__init__(parent)
        self.setObjectName("Card")

        lay = QVBoxLayout(self)
        lay.setContentsMargins(1, 1, 1, 1)
        self.view = MapView()
        lay.addWidget(self.view)

        # --- 오버레이 툴바 ---
        self.toolbar = QWidget(self)
        tb = QHBoxLayout(self.toolbar)
        tb.setContentsMargins(M.s2, M.s2, M.s2, M.s2)
        tb.setSpacing(M.s2)
        self.btn_goal = QPushButton("목표 지정")
        self.btn_wp = QPushButton("포인트 추가")
        self.btn_fit = QPushButton("전체 보기")
        for b in (self.btn_goal, self.btn_wp, self.btn_fit):
            b.setProperty("size", "sm")
            tb.addWidget(b)
        self.btn_goal.setCheckable(True)
        self.btn_wp.setCheckable(True)
        tb.addSpacing(M.s2)
        self.map_label = QLabel("맵 없음")
        self.map_label.setObjectName("SectionLabel")
        tb.addWidget(self.map_label)
        self.toolbar.setObjectName("MapOverlay")

        # --- 커서 좌표 리드아웃 ---
        self.readout = QLabel("—", self)
        self.readout.setObjectName("MapReadout")
        self.readout.setAlignment(Qt.AlignCenter)

        self.readout.setMinimumWidth(150)
        self.view.cursor_moved.connect(
            lambda x, y: self.readout.setText(f"{x:+7.2f}, {y:+7.2f}"))
        self.btn_fit.clicked.connect(self.view.fit_map)

    def set_map_label(self, map_id: str, extent: str) -> None:
        self.map_label.setText(f"{map_id} · {extent}")
        self.resizeEvent(None)

    def resizeEvent(self, ev):
        if ev is not None:
            super().resizeEvent(ev)
        self.toolbar.adjustSize()
        self.toolbar.move(M.s3, M.s3)
        self.readout.adjustSize()
        self.readout.move(self.width() - self.readout.width() - M.s3, M.s3)


class MainWindow(QMainWindow):
    def __init__(self, *, demo_mode: bool = True):
        super().__init__()
        self.setWindowTitle("SHALOM 관제 · 철도차량 하부 점검시스템")
        self.resize(1780, 1020)

        root = QWidget()
        root.setObjectName("Root")
        self.setCentralWidget(root)
        outer = QVBoxLayout(root)
        outer.setContentsMargins(M.s3, M.s3, M.s3, M.s3)
        outer.setSpacing(M.s3)

        outer.addWidget(self._build_topbar())

        split = QSplitter(Qt.Horizontal)
        split.setChildrenCollapsible(False)
        split.setHandleWidth(M.s2)

        split.addWidget(self._build_left())
        self.map_card = MapCard()
        split.addWidget(self.map_card)
        split.addWidget(self._build_right())
        split.setSizes([340, 1020, 400])
        split.setStretchFactor(1, 1)
        outer.addWidget(split, 1)

        # E-Stop 전체화면 경고 테두리
        self.alert = AlertFrame(root)
        self.alert.setGeometry(root.rect())
        root.installEventFilter(self)

        self._wire()

        self._demo = demo_mode
        if demo_mode:
            self._start_demo()

    # ================= 구성 =================
    def _build_topbar(self) -> QWidget:
        bar = QWidget()
        bar.setObjectName("TopBar")
        bar.setFixedHeight(66)
        lay = QHBoxLayout(bar)
        lay.setContentsMargins(M.s4, 0, M.s3, 0)
        lay.setSpacing(M.s4)

        title_box = QVBoxLayout()
        title_box.setSpacing(0)
        t = QLabel("SHALOM 관제")
        t.setObjectName("AppTitle")
        s = QLabel("Unitree B2 + FR3 · 철도차량 하부 점검")
        s.setObjectName("AppSubtitle")
        title_box.addWidget(t)
        title_box.addWidget(s)
        lay.addLayout(title_box)

        lay.addSpacing(M.s5)
        # 배지는 '변하는' 상태에만 쓴다. 고정 정보(맵 이름)는 지도 리드아웃으로 뺀다.
        self.badge_link = Badge("연결 끊김", "danger")
        self.badge_mission = Badge("미션 대기", "neutral")
        for b in (self.badge_link, self.badge_mission):
            lay.addWidget(b)

        lay.addStretch(1)

        self.btn_auto = QPushButton("자율")
        self.btn_manual = QPushButton("수동")
        for b in (self.btn_auto, self.btn_manual):
            b.setCheckable(True)
            b.setFixedWidth(84)
            lay.addWidget(b)
        self.btn_auto.setChecked(True)

        self.btn_theme = QPushButton("라이트")
        self.btn_theme.setProperty("size", "sm")
        self.btn_theme.setFixedWidth(58)
        self.btn_theme.setToolTip("다크 / 라이트 전환")
        lay.addWidget(self.btn_theme)

        lay.addSpacing(M.s3)
        self.estop = EStopButton(size=52)
        lay.addWidget(self.estop)

        return bar

    def _build_left(self) -> QWidget:
        host = QWidget()
        lay = QVBoxLayout(host)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(M.s3)
        self.status = StatusPanel()
        self.teleop = TeleopPanel()
        self.eventlog = EventLogPanel()
        lay.addWidget(self.status)
        lay.addWidget(self.teleop)
        lay.addWidget(self.eventlog, 1)
        host.setMinimumWidth(320)
        host.setMaximumWidth(420)
        return host

    def _build_right(self) -> QWidget:
        inner = QWidget()
        lay = QVBoxLayout(inner)
        lay.setContentsMargins(0, 0, M.s2, 0)
        lay.setSpacing(M.s3)
        self.waypoints = WaypointPanel()
        self.waypoints.setMinimumHeight(420)
        self.arm = ArmPanel()
        lay.addWidget(self.waypoints)
        lay.addWidget(self.arm)

        scroll = QScrollArea()
        scroll.setWidget(inner)
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QScrollArea.NoFrame)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        scroll.setMinimumWidth(380)
        scroll.setMaximumWidth(460)
        return scroll

    # ================= 시그널 배선 =================
    def _wire(self) -> None:
        self.btn_theme.clicked.connect(self._toggle_theme)
        self.estop.engage_requested.connect(self._engage_estop)
        self.estop.release_requested.connect(self._release_estop)

        self.btn_auto.clicked.connect(lambda: self._set_mode("auto"))
        self.btn_manual.clicked.connect(lambda: self._set_mode("manual"))

        mc = self.map_card
        mc.btn_goal.toggled.connect(
            lambda on: mc.view.set_mode(MODE_SET_GOAL if on else MODE_VIEW))
        mc.btn_wp.toggled.connect(
            lambda on: mc.view.set_mode(MODE_ADD_WAYPOINT if on else MODE_VIEW))
        mc.view.goal_requested.connect(self._on_goal)
        mc.view.waypoint_placed.connect(self._on_waypoint_placed)
        mc.view.waypoint_clicked.connect(
            lambda wid: self.eventlog.append("info", f"포인트 선택: {wid}"))

        self.teleop.cmd_vel.connect(self._on_cmd_vel)
        self.arm.preset.connect(self._on_arm_preset)
        self.arm.joint_goal.connect(
            lambda q: self.eventlog.append("info", "관절 목표 전송", "cmd/arm/joint_goal"))
        self.arm.ee_goal.connect(
            lambda g: self.eventlog.append(
                "info", f"EE 목표 X{g['x']:.2f} Y{g['y']:.2f} Z{g['z']:.2f}",
                "cmd/arm/ee_goal"))
        self.arm.stop.connect(
            lambda: self.eventlog.append("warn", "로봇팔 정지 요청", "cmd/arm/stop"))

        self.waypoints.mission_start.connect(self._mission_start)
        self.waypoints.mission_pause.connect(self._mission_pause)
        self.waypoints.mission_resume.connect(self._mission_resume)
        self.waypoints.mission_stop.connect(self._mission_stop)
        self.waypoints.add_requested.connect(lambda: mc.btn_wp.setChecked(True))
        self.waypoints.order_changed.connect(
            lambda ids: self.eventlog.append("info", f"순서 변경 ({len(ids)}개)",
                                             "cmd/waypoints/set"))

    def _toggle_theme(self) -> None:
        self.apply_theme(toggle_theme().name)

    def apply_theme(self, name: str) -> None:
        """전역 QSS 를 다시 만들어 적용하고, 직접 그리는 위젯을 갱신한다.

        QSS 로 칠해지는 위젯은 setStyleSheet 한 번으로 전부 따라온다.
        QPainter 위젯은 paint 시점에 palette() 를 조회하므로 update() 만 하면 되고,
        QGraphicsScene 의 펜 색은 아이템에 박혀 있어 retheme() 로 다시 지정한다.
        """
        set_theme(name)
        app = QApplication.instance()
        if app is not None:
            app.setStyleSheet(build_qss())
        self.btn_theme.setText("다크" if name == "light" else "라이트")
        self.map_card.view.retheme()
        for w in self.findChildren(QWidget):
            w.update()
        if self._demo:
            # 맵 이미지는 팔레트 색으로 구워져 있어 다시 렌더해야 한다
            info, grid = self._map_source
            self.map_card.view.set_map(info, occupancy_to_image(grid))

    def eventFilter(self, obj, ev):
        if obj is self.centralWidget() and ev.type() == ev.Type.Resize:
            self.alert.setGeometry(self.centralWidget().rect())
        return super().eventFilter(obj, ev)

    # ================= 동작 =================
    def _engage_estop(self) -> None:
        self.estop.set_engaged(True)
        self.alert.set_active(True)
        self.status.set_mode("", True)
        self.teleop.set_enabled(False)
        self.arm.set_enabled(False)
        self.eventlog.append("error", "E-Stop 발동 — 전 동작 정지", "cmd/estop")

    def _release_estop(self) -> None:
        # 자동 해제 금지(지시서 2.2.5). 반드시 사람이 확인한다.
        ans = QMessageBox.question(
            self, "E-Stop 해제 확인",
            "E-Stop 을 해제합니다.\n\n"
            "로봇 주변에 사람이 없고 안전이 확보되었는지 확인하십시오.\n"
            "해제 후에도 자율주행은 자동 재개되지 않으며,\n"
            "명시적 재개 명령이 필요합니다.",
            QMessageBox.Yes | QMessageBox.No, QMessageBox.No)
        if ans != QMessageBox.Yes:
            return
        self.estop.set_engaged(False)
        self.alert.set_active(False)
        self._set_mode("manual")
        self.eventlog.append("warn", "E-Stop 해제 (수동 확인)", "cmd/estop_release")

    def _set_mode(self, mode: str) -> None:
        if self.estop.is_engaged():
            self.btn_auto.setChecked(False)
            self.btn_manual.setChecked(False)
            return
        auto = mode == "auto"
        self.btn_auto.setChecked(auto)
        self.btn_manual.setChecked(not auto)
        self.status.set_mode(mode, False)
        self.teleop.set_enabled(not auto)
        self.arm.set_enabled(True)
        if not auto:
            # 수동 전환 시 자율주행 즉시 중단 (지시서 2.2.5 수동 조작 우선권)
            self.waypoints.set_mission_running(False, False)
            self.eventlog.append("warn", "수동 모드 전환 — 자율주행 중단", "cmd/mode")
        else:
            self.eventlog.append("info", "자율 모드", "cmd/mode")

    def _on_goal(self, x: float, y: float, theta: float) -> None:
        self.map_card.btn_goal.setChecked(False)
        self.eventlog.append(
            "info", f"목표 지정 X{x:.2f} Y{y:.2f} θ{math.degrees(theta):.0f}°", "cmd/goto")

    def _on_waypoint_placed(self, x: float, y: float, theta: float) -> None:
        self.map_card.btn_wp.setChecked(False)
        wps = self.waypoints.waypoints()
        n = len(wps) + 1
        wps.append({"id": f"NEW-{n:02d}", "name": f"신규 포인트 {n}",
                    "x": x, "y": y, "theta": theta, "tag_id": None, "status": "todo"})
        self.waypoints.set_waypoints(wps)
        self.map_card.view.set_waypoints(wps)
        self.eventlog.append("ok", f"포인트 추가 X{x:.2f} Y{y:.2f}", "cmd/waypoints/set")

    def _on_cmd_vel(self, vx: float, vy: float, wz: float) -> None:
        pass   # 실제 연결 시 브릿지로 pub

    def _on_arm_preset(self, name: str) -> None:
        self.arm.apply_preset_to_sliders(name)
        label = {"home": "홈", "standby": "촬영대기", "stow": "수납"}.get(name, name)
        self.eventlog.append("info", f"로봇팔 프리셋: {label}", "cmd/arm/preset")

    def _mission_start(self) -> None:
        self.waypoints.set_mission_running(True, False)
        self.badge_mission.set("자율주행 중", "info")
        self.eventlog.append("ok", "자율주행 시작", "cmd/mission/start")

    def _mission_pause(self) -> None:
        self.waypoints.set_mission_running(True, True)
        self.badge_mission.set("일시정지", "warn")
        self.eventlog.append("warn", "일시정지", "cmd/mission/pause")

    def _mission_resume(self) -> None:
        self.waypoints.set_mission_running(True, False)
        self.badge_mission.set("자율주행 중", "info")
        self.eventlog.append("ok", "재개 (명시적 명령)", "cmd/mission/resume")

    def _mission_stop(self) -> None:
        self.waypoints.set_mission_running(False, False)
        self.badge_mission.set("미션 대기", "neutral")
        self.eventlog.append("warn", "미션 정지", "cmd/mission/stop")

    # ================= 데모 =================
    def _start_demo(self) -> None:
        info, grid = demo.build_map()
        self._map_source = (info, grid)
        self.map_card.view.set_map(info, occupancy_to_image(grid))
        self.map_card.set_map_label(info.map_id, f"{info.width * info.resolution:.0f}×"
                                    f"{info.height * info.resolution:.0f} m")
        self.badge_link.set("데모 데이터", "warn")

        wps = demo.build_waypoints(info)
        self.waypoints.set_waypoints(wps)
        self.map_card.view.set_waypoints(wps)
        self.map_card.view.set_tags(demo.build_tags(wps))

        self.status.set_connected(True)
        self._set_mode("auto")
        self._mission_start()
        self.eventlog.append("ok", "SLAM 맵 로드 완료", "map/occupancy")
        self.eventlog.append("info", "브릿지 연결 (데모 합성 데이터)")
        self.eventlog.append("warn", "미등록 물체 접근 — 30cm/s 감속", "SAFE_SLOW")
        self.eventlog.append("error", "C02-P04 Apriltag 인식 실패 — 복구 로직 수행", "TAG_LOST")

        self._feed = demo.DemoFeed(wps)
        self._timer = QTimer(self)
        self._timer.setInterval(100)
        self._timer.timeout.connect(self._demo_tick)
        self._timer.start()

    def _demo_tick(self) -> None:
        s = self._feed.step(0.1)
        x, y, th = s["pose"]
        v = self.map_card.view
        v.set_robot_pose(x, y, th)
        v.set_trail(s["trail"])
        v.set_plan(s["plan"])
        v.set_tags_seen(s["seen_tags"])
        for wp in s["waypoints"]:
            v.set_waypoint_status(wp["id"], wp["status"])
            self.waypoints.set_status(wp["id"], wp["status"])

        self.status.set_pose(x, y, math.degrees(th))
        self.status.set_battery(s["soc"])
        self.status.set_system(cpu=s["cpu"], mem=s["mem"], cpu_t=s["cpu_t"],
                               gpu_t=s["gpu_t"], rtt=s["rtt"])
        self.status.set_tags(len(s["seen_tags"]))
        self.arm.set_arm_state(s["joints"], s["manip"], s["sigma"],
                               "executing" if s["manip"] > 0.02 else "planning")
