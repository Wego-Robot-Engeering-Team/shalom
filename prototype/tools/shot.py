"""UI 스크린샷 캡처 — 다크/라이트 각각."""
import sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "src"))

from PySide6.QtCore import Qt, QTimer, QEventLoop
from PySide6.QtGui import QFont, QFontDatabase
from PySide6.QtWidgets import QApplication
from shalom_gcs.main_window import MainWindow
from shalom_gcs.theme.style import build_qss

QApplication.setHighDpiScaleFactorRoundingPolicy(Qt.HighDpiScaleFactorRoundingPolicy.PassThrough)
app = QApplication(sys.argv)
for n in ("Pretendard", "Inter", "Helvetica Neue"):
    if n in QFontDatabase.families():
        app.setFont(QFont(n, 10)); break
app.setStyleSheet(build_qss())

win = MainWindow(demo_mode=True)
win.resize(1720, 990)
win.show()

def spin(ms):
    loop = QEventLoop(); QTimer.singleShot(ms, loop.quit); loop.exec()

outdir = pathlib.Path(sys.argv[1])
spin(400); win.map_card.view.fit_map(); spin(4000)
win.grab().save(str(outdir / "gcs_light.png")); print("light saved")

win.apply_theme("dark"); spin(200); win.map_card.view.fit_map(); spin(1500)
win.grab().save(str(outdir / "gcs_dark.png")); print("dark saved")
