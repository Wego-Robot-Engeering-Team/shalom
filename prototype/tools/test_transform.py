"""transform 왕복 검증 — 부호 오류를 조기에 잡는다."""
import math, sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "src"))
from shalom_gcs.mapview.transform import MapInfo

mi = MapInfo(width=400, height=300, resolution=0.05, origin_x=-10.0, origin_y=-7.5)

# 원점(좌하단 셀) 은 씬 좌하단
sx, sy = mi.to_scene_xy(-10.0, -7.5)
assert abs(sx - 0.0) < 1e-9 and abs(sy - 300.0) < 1e-9, (sx, sy)

# 격자 우상단 모서리는 씬 우상단
sx, sy = mi.to_scene_xy(-10.0 + 400 * 0.05, -7.5 + 300 * 0.05)
assert abs(sx - 400.0) < 1e-9 and abs(sy - 0.0) < 1e-9, (sx, sy)

# 왕복
for wx, wy in [(0.0, 0.0), (3.21, -1.04), (-9.9, 7.0), (5.5, 5.5)]:
    sx, sy = mi.to_scene_xy(wx, wy)
    bx, by = mi.to_world(sx, sy)
    assert abs(bx - wx) < 1e-9 and abs(by - wy) < 1e-9, (wx, wy, bx, by)

# 회전: world +90도(북쪽) → 씬에서는 위쪽(-y). 아이템 회전은 -90도.
assert abs(MapInfo.theta_to_item_rotation(math.pi / 2) + 90.0) < 1e-9
assert abs(MapInfo.item_rotation_to_theta(-90.0) - math.pi / 2) < 1e-9

# 실제로 위를 향하는지 벡터로 확인
theta = math.pi / 2
a = math.radians(MapInfo.theta_to_item_rotation(theta))
vx, vy = math.cos(a), math.sin(a)     # Qt 가 (1,0) 을 보내는 곳
assert abs(vx) < 1e-9 and abs(vy + 1.0) < 1e-9, (vx, vy)   # 씬 -y = 화면 위

print("transform 검증 통과")
