"""애플리케이션 진입점."""

from __future__ import annotations

import sys
from pathlib import Path

from PySide6.QtCore import Qt
from PySide6.QtGui import QFont, QFontDatabase
from PySide6.QtWidgets import QApplication

from .main_window import MainWindow
from .theme.style import build_qss

ASSET_FONTS = Path(__file__).resolve().parents[2] / "assets" / "fonts"


def _load_bundled_fonts() -> None:
    """번들 폰트를 등록한다.

    Windows 기본 한글 폰트(맑은 고딕)는 화면 인상을 크게 떨어뜨린다.
    Pretendard(SIL OFL 1.1) 를 assets/fonts 에 넣으면 OS 무관하게 동일한
    렌더링이 나온다. 없으면 조용히 시스템 폰트로 폴백한다.
    """
    if not ASSET_FONTS.is_dir():
        return
    for f in ASSET_FONTS.glob("*.[to]tf"):
        QFontDatabase.addApplicationFont(str(f))


def main(argv: list[str] | None = None) -> int:
    argv = list(sys.argv if argv is None else argv)
    demo_mode = "--live" not in argv

    QApplication.setHighDpiScaleFactorRoundingPolicy(
        Qt.HighDpiScaleFactorRoundingPolicy.PassThrough)
    app = QApplication(argv)
    app.setApplicationName("SHALOM GCS")
    app.setOrganizationName("WEGO Robotics")

    _load_bundled_fonts()
    families = QFontDatabase.families()
    for name in ("Pretendard Variable", "Pretendard", "Inter", "Helvetica Neue"):
        if name in families:
            app.setFont(QFont(name, 10))
            break

    app.setStyleSheet(build_qss())

    win = MainWindow(demo_mode=demo_mode)
    win.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
