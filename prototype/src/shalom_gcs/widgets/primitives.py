"""레이아웃 프리미티브 — Card, Badge, 구분선, 폼 행.

스타일은 전부 QSS(objectName 기준)로 처리한다. 인라인 setStyleSheet 을 쓰지
않는 이유는 테마 전환 시 다시 칠해야 할 곳이 늘어나기 때문이다.
"""

from __future__ import annotations

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (
    QFrame,
    QHBoxLayout,
    QLabel,
    QSizePolicy,
    QVBoxLayout,
    QWidget,
)

from ..theme.style import repolish
from ..theme.tokens import METRICS as M


class Card(QFrame):
    """제목 헤더 + 본문을 가진 표준 패널.

    본문 위젯은 `card.body` 레이아웃에 넣고,
    헤더 우측 부가 정보는 `card.add_header_widget(w)` 로 붙인다.
    """

    def __init__(self, title: str = "", parent: QWidget | None = None, *, padded: bool = True):
        super().__init__(parent)
        self.setObjectName("Card")

        outer = QVBoxLayout(self)
        outer.setContentsMargins(0, 0, 0, 0)
        outer.setSpacing(0)

        self._header: QWidget | None = None
        if title:
            self._header = QWidget()
            self._header.setObjectName("CardHeader")
            self._header.setFixedHeight(34)
            hl = QHBoxLayout(self._header)
            hl.setContentsMargins(M.s3, 0, M.s2, 0)
            hl.setSpacing(M.s2)

            lbl = QLabel(title)
            lbl.setObjectName("CardTitle")
            hl.addWidget(lbl)
            hl.addStretch(1)
            self._header_layout = hl
            outer.addWidget(self._header)

        body_host = QWidget()
        pad = M.s3 if padded else 0
        self.body = QVBoxLayout(body_host)
        self.body.setContentsMargins(pad, pad, pad, pad)
        self.body.setSpacing(M.s2)
        outer.addWidget(body_host, 1)

    def add_header_widget(self, w: QWidget) -> None:
        if self._header is None:
            raise RuntimeError("제목 없는 Card 에는 헤더 위젯을 붙일 수 없다")
        self._header_layout.addWidget(w)


class Badge(QLabel):
    """상태 표시. tone: neutral | ok | warn | danger | info

    상태가 실제로 '변하는' 자리에만 쓴다. 고정 라벨에 배지를 쓰면
    화면이 알록달록해지고 정작 변화가 눈에 안 들어온다.
    """

    def __init__(self, text: str = "", tone: str = "neutral", parent: QWidget | None = None):
        super().__init__(text, parent)
        self.setObjectName("Badge")
        self.setAlignment(Qt.AlignCenter)
        self.setSizePolicy(QSizePolicy.Maximum, QSizePolicy.Fixed)
        self.setProperty("tone", tone)

    def set_tone(self, tone: str) -> None:
        if self.property("tone") != tone:
            self.setProperty("tone", tone)
            repolish(self)

    def set(self, text: str, tone: str) -> None:
        self.setText(text)
        self.set_tone(tone)


class HLine(QFrame):
    def __init__(self, parent: QWidget | None = None):
        super().__init__(parent)
        self.setObjectName("HLine")
        self.setFixedHeight(1)


def section_label(text: str) -> QLabel:
    lbl = QLabel(text)
    lbl.setObjectName("SectionLabel")
    return lbl


def readout(text: str = "—", *, large: bool = False) -> QLabel:
    lbl = QLabel(text)
    lbl.setObjectName("ReadoutLg" if large else "Readout")
    return lbl


def field_row(label: str, widget: QWidget, *, label_width: int = 58) -> QWidget:
    """'라벨 ─ 위젯' 한 줄. 폼 정렬을 일관되게 유지한다."""
    host = QWidget()
    lay = QHBoxLayout(host)
    lay.setContentsMargins(0, 0, 0, 0)
    lay.setSpacing(M.s2)
    lbl = section_label(label)
    lbl.setFixedWidth(label_width)
    lay.addWidget(lbl)
    lay.addWidget(widget, 1)
    return host
