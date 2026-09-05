"""QSS 스타일시트 생성.

전역 스타일시트 하나만 QApplication 에 건다. 위젯별 setStyleSheet 은 쓰지 않는다
— 테마 전환 시 한 곳만 다시 만들면 전부 따라오게 하기 위함이다.

동적 상태는 Qt 프로퍼티 셀렉터로 처리한다:
    btn.setProperty("variant", "danger"); repolish(btn)
"""

from __future__ import annotations

from PySide6.QtGui import QColor
from PySide6.QtWidgets import QWidget

from .tokens import METRICS as M
from .tokens import TYPE as T
from .tokens import Palette, palette


def rgba(hex_color: str, alpha: float) -> str:
    """QSS 용 rgba() 문자열.

    ⚠️ QSS 의 8자리 hex 는 `#AARRGGBB` 순서다. CSS 습관대로 `#RRGGBBAA` 로 쓰면
    알파가 적색 채널로 들어가 전혀 다른 색이 된다(흰색 + D9 → 연노랑).
    혼동을 없애기 위해 반투명 색은 전부 이 함수를 거친다.
    """
    c = QColor(hex_color)
    return f"rgba({c.red()}, {c.green()}, {c.blue()}, {alpha:.3f})"


def repolish(w: QWidget) -> None:
    """프로퍼티 셀렉터는 자동 재평가되지 않는다. 값 변경 후 반드시 호출."""
    w.style().unpolish(w)
    w.style().polish(w)
    w.update()


def build_qss(pal: Palette | None = None) -> str:
    P = pal or palette()
    overlay_bg = rgba(P.overlay, 0.90)     # 지도 위 반투명 칩
    accent_soft = rgba(P.accent, 0.12)     # 선택 상태 배경

    return f"""
/* ===================== 전역 ===================== */
* {{
    font-family: {T.ui};
    font-size: {T.md}px;
    color: {P.text};
    outline: none;
}}
QWidget {{ background: transparent; }}
QMainWindow, #Root {{ background: {P.bg}; }}

QToolTip {{
    background: {P.surface_hi};
    color: {P.text};
    border: 1px solid {P.border_hi};
    border-radius: {M.r_sm}px;
    padding: 3px 7px;
}}

QMessageBox {{ background: {P.surface}; }}

/* ===================== 면 ===================== */
/* 그림자 없음. 경계선 한 줄로만 층을 나눈다. */
#Card, #TopBar {{
    background: {P.surface};
    border: 1px solid {P.border};
    border-radius: {M.r_lg}px;
}}
#CardHeader {{
    background: transparent;
    border: none;
    border-bottom: 1px solid {P.border};
}}
#CardTitle {{
    font-size: {T.md}px;
    font-weight: 600;
    color: {P.text};
}}
#SectionLabel {{
    font-size: {T.sm}px;
    color: {P.text_mute};
}}
#Hint {{ color: {P.text_mute}; font-size: {T.sm}px; }}
#HLine {{ background: {P.border}; border: none; }}

#AppTitle {{ font-size: {T.xl}px; font-weight: 650; color: {P.text}; }}
#AppSubtitle {{ font-size: {T.sm}px; color: {P.text_mute}; }}

/* ===================== 버튼 ===================== */
QPushButton {{
    background: {P.surface_hi};
    border: 1px solid {P.border_hi};
    border-radius: {M.r_md}px;
    padding: 0 10px;
    min-height: {M.ctl_h}px;
    font-weight: 500;
    color: {P.text};
}}
QPushButton:hover   {{ background: {P.surface_hover}; }}
QPushButton:pressed {{ background: {P.border}; }}
QPushButton:disabled {{
    background: transparent;
    color: {P.text_mute};
    border-color: {P.border};
}}
QPushButton:checked {{
    background: {accent_soft};
    border-color: {P.accent};
    color: {P.accent};
}}

QPushButton[variant="primary"] {{
    background: {P.accent}; border-color: {P.accent};
    color: {P.text_on_accent}; font-weight: 600;
}}
QPushButton[variant="primary"]:hover   {{ background: {P.accent_hi}; border-color: {P.accent_hi}; }}
QPushButton[variant="primary"]:pressed {{ background: {P.accent_lo}; }}

QPushButton[variant="danger"] {{
    background: transparent; border-color: {P.danger};
    color: {P.danger}; font-weight: 600;
}}
QPushButton[variant="danger"]:hover {{
    background: {P.danger}; border-color: {P.danger}; color: {P.text_on_accent};
}}

QPushButton[variant="ghost"] {{
    background: transparent; border-color: transparent; color: {P.text_dim};
}}
QPushButton[variant="ghost"]:hover {{ background: {P.surface_hi}; color: {P.text}; }}

QPushButton[size="sm"] {{
    min-height: {M.ctl_h_sm}px; padding: 0 8px; font-size: {T.sm}px;
}}

/* ===================== 입력 ===================== */
QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QPlainTextEdit {{
    background: {P.surface_hi};
    border: 1px solid {P.border_hi};
    border-radius: {M.r_md}px;
    padding: 0 7px;
    min-height: {M.ctl_h_sm}px;
    font-family: {T.mono};
    selection-background-color: {P.accent};
    selection-color: {P.text_on_accent};
}}
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus {{
    border-color: {P.accent};
}}
QComboBox {{ font-family: {T.ui}; }}
QComboBox::drop-down {{ border: none; width: 18px; }}
QComboBox QAbstractItemView {{
    background: {P.surface};
    border: 1px solid {P.border_hi};
    selection-background-color: {accent_soft};
    padding: 2px;
}}
QSpinBox::up-button, QSpinBox::down-button,
QDoubleSpinBox::up-button, QDoubleSpinBox::down-button {{ width: 0; border: none; }}

/* ===================== 슬라이더 ===================== */
QSlider::groove:horizontal {{
    height: 3px; background: {P.border_hi}; border-radius: 1px;
}}
QSlider::sub-page:horizontal {{ background: {P.accent}; border-radius: 1px; }}
QSlider::handle:horizontal {{
    background: {P.surface};
    border: 2px solid {P.accent};
    width: 10px; height: 10px; border-radius: 7px; margin: -6px 0;
}}
QSlider::handle:horizontal:hover {{ background: {P.accent}; }}
QSlider::groove:horizontal:disabled   {{ background: {P.border}; }}
QSlider::sub-page:horizontal:disabled {{ background: {P.text_mute}; }}
QSlider::handle:horizontal:disabled   {{ border-color: {P.text_mute}; }}

QSlider[warn="true"]::sub-page:horizontal {{ background: {P.warning}; }}
QSlider[warn="true"]::handle:horizontal   {{ border-color: {P.warning}; }}

/* ===================== 리스트 ===================== */
QListWidget, QTreeWidget, QTableWidget {{ background: transparent; border: none; }}
QListWidget::item {{ border: none; }}
QHeaderView::section {{
    background: transparent; border: none;
    border-bottom: 1px solid {P.border};
    color: {P.text_mute}; font-size: {T.sm}px;
    padding: 3px 6px;
}}

/* ===================== 스크롤바 ===================== */
QScrollArea {{ background: transparent; border: none; }}
QScrollBar:vertical   {{ background: transparent; width: 9px; margin: 0; }}
QScrollBar:horizontal {{ background: transparent; height: 9px; margin: 0; }}
QScrollBar::handle:vertical, QScrollBar::handle:horizontal {{
    background: {P.border_hi}; border-radius: 4px; min-height: 24px; min-width: 24px;
}}
QScrollBar::handle:hover {{ background: {P.text_mute}; }}
QScrollBar::add-line, QScrollBar::sub-line {{ height: 0; width: 0; }}
QScrollBar::add-page, QScrollBar::sub-page {{ background: transparent; }}

/* ===================== 스플리터 ===================== */
QSplitter::handle {{ background: transparent; }}
QSplitter::handle:horizontal {{ width: {M.s3}px; }}
QSplitter::handle:vertical   {{ height: {M.s3}px; }}

/* ===================== 배지 ===================== */
/* 남용하지 않는다. 상태가 '변할 때'만 의미가 있는 자리에만 쓴다. */
#Badge {{
    background: transparent;
    border: 1px solid {P.border_hi};
    border-radius: {M.r_sm}px;
    padding: 1px 6px;
    font-size: {T.sm}px;
    font-weight: 500;
    color: {P.text_dim};
}}
#Badge[tone="ok"]     {{ color: {P.success}; border-color: {rgba(P.success, 0.40)}; }}
#Badge[tone="warn"]   {{ color: {P.warning}; border-color: {rgba(P.warning, 0.40)}; }}
#Badge[tone="danger"] {{ color: {P.danger};  border-color: {rgba(P.danger, 0.40)}; }}
#Badge[tone="info"]   {{ color: {P.accent};  border-color: {rgba(P.accent, 0.40)}; }}

/* ===================== 수치 ===================== */
#Mono      {{ font-family: {T.mono}; color: {P.text_dim}; font-size: {T.sm}px; }}
#Readout   {{ font-family: {T.mono}; color: {P.text}; font-size: {T.md}px; font-weight: 500; }}
#ReadoutLg {{ font-family: {T.mono}; color: {P.text}; font-size: {T.lg}px; font-weight: 600; }}

/* ===================== 지도 오버레이 ===================== */
#MapOverlay {{
    background: {overlay_bg};
    border: 1px solid {P.border_hi};
    border-radius: {M.r_md}px;
}}
#MapReadout {{
    background: {overlay_bg};
    border: 1px solid {P.border_hi};
    border-radius: {M.r_md}px;
    padding: 3px 8px;
    font-family: {T.mono};
    font-size: {T.sm}px;
    color: {P.text_dim};
}}
"""
