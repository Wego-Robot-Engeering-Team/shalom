"""이벤트 로그 패널 — 과업지시서 2.2.7 [5] ③.

사람 감지·미등록 장애물·통신 두절·배터리 부족 이력을 시간순으로 쌓는다.
"""

from __future__ import annotations

import time

from PySide6.QtCore import QRectF, QSize, Qt
from PySide6.QtGui import QColor, QFont, QPainter
from PySide6.QtWidgets import (
    QAbstractItemView,
    QListWidget,
    QListWidgetItem,
    QStyle,
    QStyledItemDelegate,
    QWidget,
)

from ..theme.tokens import METRICS as M
from ..theme.tokens import mono_family, palette
from ..widgets.primitives import Badge, Card

MAX_ROWS = 500


def level_color(level: str) -> str:
    P = palette()
    return {"info": P.text_dim, "ok": P.success,
            "warn": P.warning, "error": P.danger}.get(level, P.text_dim)


class _LogDelegate(QStyledItemDelegate):
    def sizeHint(self, opt, idx) -> QSize:
        return QSize(0, 34)

    def paint(self, p: QPainter, opt, idx) -> None:
        p.save()
        p.setRenderHint(QPainter.Antialiasing)
        P = palette()
        r = opt.rect.adjusted(0, 1, 0, -1)
        d = idx.data(Qt.UserRole) or {}
        col = QColor(level_color(d.get("level", "info")))

        if opt.state & QStyle.StateFlag.State_MouseOver:
            p.setPen(Qt.NoPen); p.setBrush(QColor(P.surface_hi))
            p.drawRect(r)

        # 좌측 레벨 표시 — 얇은 막대 하나로 끝낸다
        p.setPen(Qt.NoPen); p.setBrush(col)
        p.drawRect(QRectF(r.left(), r.top() + 4, 2, r.height() - 8))

        f = QFont(mono_family()); f.setPointSize(9)
        p.setFont(f)
        p.setPen(QColor(P.text_mute))
        p.drawText(r.adjusted(10, 1, 0, 0), Qt.AlignLeft | Qt.AlignTop, d.get("time", ""))

        f2 = QFont(); f2.setPointSize(10)
        p.setFont(f2)
        p.setPen(QColor(P.text))
        p.drawText(r.adjusted(66, 1, -6, 0), Qt.AlignLeft | Qt.AlignTop, d.get("msg", ""))

        if d.get("code"):
            f3 = QFont(mono_family()); f3.setPointSize(8)
            p.setFont(f3); p.setPen(QColor(P.text_mute))
            p.drawText(r.adjusted(66, 0, -6, -2), Qt.AlignLeft | Qt.AlignBottom, d["code"])
        p.restore()


class EventLogPanel(Card):
    def __init__(self, parent: QWidget | None = None):
        super().__init__("이벤트 로그", parent)
        self.badge = Badge("0", "neutral")
        self.add_header_widget(self.badge)

        self.list = QListWidget()
        self.list.setItemDelegate(_LogDelegate(self.list))
        self.list.setVerticalScrollMode(QAbstractItemView.ScrollPerPixel)
        self.list.setSelectionMode(QAbstractItemView.NoSelection)
        self.body.addWidget(self.list, 1)
        self._warn_count = 0

    def append(self, level: str, msg: str, code: str = "") -> None:
        it = QListWidgetItem()
        it.setData(Qt.UserRole, {
            "time": time.strftime("%H:%M:%S"),
            "level": level, "msg": msg, "code": code,
        })
        self.list.insertItem(0, it)
        while self.list.count() > MAX_ROWS:
            self.list.takeItem(self.list.count() - 1)
        if level in ("warn", "error"):
            self._warn_count += 1
            self.badge.set(str(self._warn_count),
                           "danger" if level == "error" else "warn")
