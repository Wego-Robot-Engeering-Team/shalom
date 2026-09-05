"""점검포인트 시퀀스 패널 — 과업지시서 2.2.7 [2] ①.

추가·삭제·순서 변경·자율주행 시작/일시정지/재개/정지.
순서 변경은 드래그 앤 드롭으로 처리하고, 배열 순서가 곧 순회 순서다.

주의: '재개'는 명시적 명령으로만 동작한다(지시서 2.2.5 수동 조작 우선권).
UI 가 임의로 자동 재개를 보내지 않는다.
"""

from __future__ import annotations

from PySide6.QtCore import QSize, Qt, Signal
from PySide6.QtGui import QColor, QFont, QPainter, QPen
from PySide6.QtWidgets import (
    QAbstractItemView,
    QHBoxLayout,
    QListWidget,
    QListWidgetItem,
    QStyle,
    QStyledItemDelegate,
    QWidget,
)

from ..mapview.items import wp_color
from ..theme.tokens import METRICS as M
from ..theme.tokens import mono_family, palette
from ..widgets.primitives import Badge, Card

STATUS_LABEL = {"done": "완료", "current": "진행", "todo": "대기", "error": "오류"}


class _WaypointDelegate(QStyledItemDelegate):
    """행 하나를 직접 그린다 — 상태 점, 순번, 이름, 좌표, 상태 라벨."""

    def sizeHint(self, opt, idx) -> QSize:
        return QSize(0, 40)

    def paint(self, p: QPainter, opt, idx) -> None:
        p.save()
        p.setRenderHint(QPainter.Antialiasing)
        P = palette()
        r = opt.rect.adjusted(0, 0, 0, 0)
        data = idx.data(Qt.UserRole) or {}
        status = data.get("status", "todo")
        col = QColor(wp_color(status))

        selected = bool(opt.state & QStyle.StateFlag.State_Selected)
        hovered = bool(opt.state & QStyle.StateFlag.State_MouseOver)
        if selected:
            sel = QColor(P.accent); sel.setAlpha(30)
            p.setPen(Qt.NoPen); p.setBrush(sel)
            p.drawRect(r)
        elif hovered:
            p.setPen(Qt.NoPen); p.setBrush(QColor(P.surface_hi))
            p.drawRect(r)

        # 상태 점
        cy = r.center().y()
        p.setPen(Qt.NoPen if status != "todo" else QPen(col, 1.2))
        p.setBrush(col if status != "todo" else Qt.NoBrush)
        p.drawEllipse(r.left() + 10, cy - 4, 8, 8)

        f = QFont(); f.setPointSize(11)
        f.setWeight(QFont.DemiBold if status == "current" else QFont.Normal)
        p.setFont(f)
        p.setPen(QColor(P.text) if status != "todo" else QColor(P.text_dim))
        p.drawText(r.adjusted(26, 3, -62, 0), Qt.AlignLeft | Qt.AlignTop,
                   f"{idx.row() + 1}.  {data.get('name', data.get('id', '—'))}")

        f2 = QFont(mono_family()); f2.setPointSize(9)
        p.setFont(f2)
        p.setPen(QColor(P.text_mute))
        tag = data.get("tag_id")
        sub = f"{data.get('x', 0):+.2f}, {data.get('y', 0):+.2f}"
        if tag is not None:
            sub += f"   tag {tag}"
        p.drawText(r.adjusted(26, 0, -62, -3), Qt.AlignLeft | Qt.AlignBottom, sub)

        f3 = QFont(); f3.setPointSize(9)
        p.setFont(f3)
        p.setPen(col if status != "todo" else QColor(P.text_mute))
        p.drawText(r.adjusted(0, 0, -10, 0), Qt.AlignRight | Qt.AlignVCenter,
                   STATUS_LABEL.get(status, status))
        p.restore()


class WaypointPanel(Card):
    add_requested = Signal()
    delete_requested = Signal(str)
    order_changed = Signal(list)         # [wp_id, ...]
    selected = Signal(str)
    mission_start = Signal()
    mission_pause = Signal()
    mission_resume = Signal()
    mission_stop = Signal()

    def __init__(self, parent: QWidget | None = None):
        super().__init__("점검포인트 시퀀스", parent)
        self.count_badge = Badge("0", "neutral")
        self.add_header_widget(self.count_badge)

        self.list = QListWidget()
        self.list.setItemDelegate(_WaypointDelegate(self.list))
        self.list.setDragDropMode(QAbstractItemView.InternalMove)
        self.list.setSelectionMode(QAbstractItemView.SingleSelection)
        self.list.setVerticalScrollMode(QAbstractItemView.ScrollPerPixel)
        self.list.setSpacing(1)
        self.list.model().rowsMoved.connect(self._emit_order)
        self.list.currentItemChanged.connect(self._emit_selected)
        self.body.addWidget(self.list, 1)

        # ---- 편집 버튼 ----
        edit = QHBoxLayout(); edit.setSpacing(M.s2)
        self.btn_add = self._mk("+ 지도에서 추가")
        self.btn_del = self._mk("삭제")
        self.btn_up = self._mk("↑", width=36)
        self.btn_down = self._mk("↓", width=36)
        for b in (self.btn_add, self.btn_del, self.btn_up, self.btn_down):
            b.setProperty("size", "sm")
        edit.addWidget(self.btn_add, 1)
        edit.addWidget(self.btn_del)
        edit.addWidget(self.btn_up)
        edit.addWidget(self.btn_down)
        self.body.addLayout(edit)

        self.btn_add.clicked.connect(self.add_requested)
        self.btn_del.clicked.connect(self._delete_current)
        self.btn_up.clicked.connect(lambda: self._move(-1))
        self.btn_down.clicked.connect(lambda: self._move(1))

        # ---- 미션 제어 ----
        run = QHBoxLayout(); run.setSpacing(M.s2)
        self.btn_start = self._mk("자율주행 시작")
        self.btn_start.setProperty("variant", "primary")
        self.btn_pause = self._mk("일시정지")
        self.btn_resume = self._mk("재개")
        self.btn_stop = self._mk("정지")
        run.addWidget(self.btn_start, 2)
        run.addWidget(self.btn_pause, 1)
        run.addWidget(self.btn_resume, 1)
        run.addWidget(self.btn_stop, 1)
        self.body.addLayout(run)

        self.btn_start.clicked.connect(self.mission_start)
        self.btn_pause.clicked.connect(self.mission_pause)
        self.btn_resume.clicked.connect(self.mission_resume)
        self.btn_stop.clicked.connect(self.mission_stop)
        self.btn_resume.setEnabled(False)

    @staticmethod
    def _mk(text: str, width: int | None = None):
        from PySide6.QtWidgets import QPushButton
        b = QPushButton(text)
        if width:
            b.setFixedWidth(width)
        return b

    # ---- 데이터 ----
    def set_waypoints(self, wps: list[dict]) -> None:
        self.list.blockSignals(True)
        self.list.clear()
        for wp in wps:
            it = QListWidgetItem()
            it.setData(Qt.UserRole, wp)
            self.list.addItem(it)
        self.list.blockSignals(False)
        self.count_badge.setText(str(len(wps)))

    def waypoints(self) -> list[dict]:
        return [self.list.item(i).data(Qt.UserRole) for i in range(self.list.count())]

    def set_status(self, wp_id: str, status: str) -> None:
        for i in range(self.list.count()):
            it = self.list.item(i)
            d = it.data(Qt.UserRole)
            if d.get("id") == wp_id:
                d["status"] = status
                it.setData(Qt.UserRole, d)
                self.list.update(self.list.indexFromItem(it))
                return

    def set_mission_running(self, running: bool, paused: bool) -> None:
        self.btn_start.setEnabled(not running)
        self.btn_pause.setEnabled(running and not paused)
        self.btn_resume.setEnabled(running and paused)
        self.btn_stop.setEnabled(running)

    # ---- 내부 ----
    def _emit_order(self, *_a) -> None:
        self.order_changed.emit([w.get("id") for w in self.waypoints()])

    def _emit_selected(self, cur, _prev) -> None:
        if cur is not None:
            self.selected.emit((cur.data(Qt.UserRole) or {}).get("id", ""))

    def _delete_current(self) -> None:
        it = self.list.currentItem()
        if it is not None:
            self.delete_requested.emit((it.data(Qt.UserRole) or {}).get("id", ""))

    def _move(self, delta: int) -> None:
        row = self.list.currentRow()
        new = row + delta
        if row < 0 or not (0 <= new < self.list.count()):
            return
        it = self.list.takeItem(row)
        self.list.insertItem(new, it)
        self.list.setCurrentRow(new)
        self._emit_order()
