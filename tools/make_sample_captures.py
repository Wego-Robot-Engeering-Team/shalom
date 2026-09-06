#!/usr/bin/env python3
"""이력 화면을 확인하기 위한 표본 촬영 데이터를 만든다.

실제 촬영본이 나오기 전까지 쓰는 대역이다. 이 폴더를 설정 · 저장의
"저장 장치 경로" 로 지정하면 이력 화면이 채워진다.

파일 이름은 납품 규정 형식을 그대로 따른다:

    편성번호_량번호_포인트ID,yyyyMMddHHmmss.jpg
    편성번호_량번호_포인트ID,yyyyMMddHHmmss.json   (사이드카)

이미지는 합성이다. 실제 사진처럼 보이게 만들 이유가 없고, 오히려 표본이
실물로 오해되면 곤란하다. 대신 어떤 자리에서 무엇을 찍은 것인지(측면인지
하부인지, 몇 량 어느 지점인지)를 그림 안에 적어 둔다.

사용법:
    python3 tools/make_sample_captures.py ~/inspection-samples
"""

import json
import math
import random
import struct
import sys
import zlib
from datetime import datetime, timedelta
from pathlib import Path

TRAIN = "1234"
CARS = 8
UNDER_PER_CAR = 4
SIDE_PER_CAR = 2

W, H = 640, 480


def png_bytes(pixels, w, h):
    """PNG 로 내보낸다. PPM 은 한 장에 2 MB 라 64 장이 100 MB 를 넘고,
    그런 것을 저장소에 올릴 수는 없다. zlib 은 표준 라이브러리라 이
    스크립트를 돌리는 데 추가 패키지가 필요 없다."""
    raw = bytearray()
    for y in range(h):
        raw.append(0)                       # 필터 없음
        raw += pixels[y * w * 3:(y + 1) * w * 3]

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
            + chunk(b"IEND", b""))


class Canvas:
    """의존성 없이 쓰는 작은 래스터. Pillow 없이도 돌아야 한다 —
    이 스크립트 하나 때문에 빌드 환경에 패키지를 요구할 수 없다."""

    def __init__(self, w, h, rgb):
        self.w, self.h = w, h
        self.px = bytearray(bytes(rgb) * (w * h))

    def set(self, x, y, rgb):
        if 0 <= x < self.w and 0 <= y < self.h:
            i = (y * self.w + x) * 3
            self.px[i:i + 3] = bytes(rgb)

    def rect(self, x0, y0, x1, y1, rgb):
        for y in range(max(0, int(y0)), min(self.h, int(y1))):
            base = y * self.w * 3
            for x in range(max(0, int(x0)), min(self.w, int(x1))):
                self.px[base + x * 3: base + x * 3 + 3] = bytes(rgb)

    def noise(self, amount, seed):
        rng = random.Random(seed)
        for i in range(0, len(self.px), 3):
            d = rng.randint(-amount, amount)
            for k in range(3):
                self.px[i + k] = max(0, min(255, self.px[i + k] + d))

    def save(self, path: Path):
        path.write_bytes(png_bytes(self.px, self.w, self.h))


def side_view(seed):
    """차량 옆면. 창과 도색 띠가 있는 밝은 차체."""
    c = Canvas(W, H, (150, 156, 163))
    c.rect(0, 0, W, H * 0.18, (176, 184, 192))          # 검수고 천장 조명
    c.rect(0, H * 0.18, W, H * 0.78, (198, 202, 206))   # 차체
    c.rect(0, H * 0.30, W, H * 0.34, (60, 72, 88))      # 창 띠
    for x in range(40, W - 40, 150):                     # 창
        c.rect(x, H * 0.31, x + 110, H * 0.44, (36, 44, 56))
    c.rect(0, H * 0.52, W, H * 0.56, (40, 96, 170))     # 청색 띠
    c.rect(0, H * 0.56, W, H * 0.59, (150, 130, 90))    # 금색 띠
    c.rect(0, H * 0.78, W, H, (96, 100, 106))           # 대차·바닥
    for x in range(120, W - 120, 380):                   # 차륜
        c.rect(x, H * 0.80, x + 150, H * 0.96, (58, 62, 68))
    c.noise(7, seed)
    return c


def under_view(seed):
    """차량 하부. 어둡고 배관과 구조물이 지나간다."""
    c = Canvas(W, H, (58, 56, 54))
    c.rect(0, 0, W, H * 0.34, (44, 42, 41))             # 차체 바닥면
    for y in (0.40, 0.56, 0.72):                         # 배관
        c.rect(0, H * y, W, H * (y + 0.05), (86, 82, 76))
    for x in range(60, W, 240):                          # 횡보
        c.rect(x, H * 0.30, x + 34, H, (72, 70, 66))
    c.rect(0, H * 0.90, W, H, (40, 38, 36))             # 궤도
    c.noise(11, seed)
    return c


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 2

    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)

    base = datetime(2026, 9, 5, 9, 12, 0)
    train_len = CARS * 20.0 + (CARS - 1) * 0.6
    x_start = -train_len / 2.0

    n = 0
    tag = 10
    for car in range(CARS):
        cx0 = x_start + car * 20.6

        spots = []
        for i in range(UNDER_PER_CAR):
            spots.append((f"U{car + 1}-{i + 1}", cx0 + 20.0 * (i + 0.5) / UNDER_PER_CAR,
                          0.0, 0.0, "under"))
        for side, (y, th) in enumerate(((3.05, -math.pi / 2), (-3.05, math.pi / 2))):
            key = "L" if side == 0 else "R"
            for i in range(SIDE_PER_CAR):
                spots.append((f"{key}{car + 1}-{i + 1}",
                              cx0 + 20.0 * (i + 0.5) / SIDE_PER_CAR, y, th, "side"))

        for point_id, x, y, theta, kind in spots:
            stamp = base + timedelta(seconds=n * 47)
            stem = f"{TRAIN}_{car + 1:02d}_{point_id},{stamp:%Y%m%d%H%M%S}"

            img = under_view(n) if kind == "under" else side_view(n)
            img.save(out / f"{stem}.png")

            (out / f"{stem}.json").write_text(json.dumps({
                "vehicle_number": TRAIN,
                "car_number": f"{car + 1:02d}",
                "point_id": point_id,
                "captured_at": stamp.strftime("%Y-%m-%dT%H:%M:%S"),
                "view": kind,
                "tag_id": tag,
                "robot_pose": {"x": round(x, 2), "y": round(y, 2),
                               "theta_deg": round(math.degrees(theta), 1)},
                "distance_mm": 620 if kind == "under" else 1500,
                "resolution": f"{W}x{H}",
            }, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

            n += 1
            tag += 1

    print(f"{out}  표본 {n} 건 ({CARS} 량 × 하부 {UNDER_PER_CAR} + 측면 {SIDE_PER_CAR * 2})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
