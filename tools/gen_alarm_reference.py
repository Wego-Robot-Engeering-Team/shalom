#!/usr/bin/env python3
# Copyright (c) 2026 WeGo Robotics. All rights reserved.

"""hmi/resources/error_codes.json 에서 docs/alarm_reference.md 를 만든다.

카탈로그가 단일 출처다. 표를 손으로 쓰면 화면이 띄우는 내용과 어긋나고,
어긋난 사실은 검수 자리나 현장에서야 드러난다.

    python3 tools/gen_alarm_reference.py          문서를 다시 만든다
    python3 tools/gen_alarm_reference.py --check  어긋나면 0 이 아닌 값으로 끝난다
"""

import argparse
import collections
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
CATALOG = ROOT / "hmi/resources/error_codes.json"
OUTPUT = ROOT / "docs/alarm_reference.md"

# 접두사 순서가 곧 문서의 절 순서다.
BANDS = [
    ("E_", "명령 거절"), ("LINK_", "전송"), ("ROBOT_", None), ("SYS_", "시스템"),
    ("TF_", "좌표계"), ("MAP_", "지도"), ("UPDATE_", "배포"), ("SAFETY_", "안전"),
    ("MOTION_", "모션 권한"), ("NAV_", "자율주행"), ("MISSION_", "점검"),
    ("SETUP_", "사전 교시"), ("PERCEPT_", "인식"), ("ARM_", "로봇팔"),
    ("BASE_", "본체"), ("CAPTURE_", "촬영"), ("POWER_", "전원"),
    ("AUDIT_", "조작 이력"),
]
SEVERITY = {"critical": "치명", "error": "오류", "warn": "경고",
            "info": "정보", "ok": "정상"}
ORDER = {"critical": 0, "error": 1, "warn": 2, "info": 3, "ok": 4}

HEAD = """# 알람 코드 표

점검 로봇이 내는 코드 전부와 그 조치다. 조작자가 코드를 보고 무엇을 해야 하는지
찾는 용도이며, 비상대응 매뉴얼의 근거 자료다.

**이 문서는 `hmi/resources/error_codes.json` 에서 생성한다. 직접 고치지 말 것.**
`python3 tools/gen_alarm_reference.py` 로 다시 만든다.

## 읽는 법

| 등급 | 뜻 |
| --- | --- |
| 치명 | 운용을 멈추고 조치해야 한다 |
| 오류 | 해당 동작이 실패했다. 원인을 없애야 이어갈 수 있다 |
| 경고 | 동작은 이어지나 확인이 필요하다 |
| 정보 · 정상 | 알림. 조치가 필요 없다 |

`유지` 표시가 있는 코드는 원인이 사라져도 화면에 남는다. 사람이 확인하고 해제해야
한다. 나머지는 원인이 없어지거나 짝 코드가 오면 사라진다.

코드 번호 앞자리로 영역을 구분한다 — 1 전송·시스템, 2 안전, 3 주행·점검,
4 인식, 5 로봇팔·본체, 6 촬영, 7 전원, 8 조작 이력.
"""


def band_of(code):
    return max((p for p, _ in BANDS if code.startswith(p)), key=len)


def render(catalog):
    codes = catalog["codes"]
    buckets = collections.defaultdict(list)
    for c in codes:
        buckets[band_of(c["code"])].append(c)

    lines = [HEAD]
    for prefix, name in BANDS:
        if name is None:          # 다른 절에 합쳐지는 접두사
            continue
        rows = list(buckets.get(prefix, []))
        if prefix == "LINK_":     # ROBOT_SELECTED 는 전송 절에 둔다
            rows += buckets.get("ROBOT_", [])
        if not rows:
            continue
        lines += [f"## {name} — `{prefix}` ({len(rows)})", "",
                  "| 코드 | 번호 | 등급 | 발생처 | 메시지 | 원인 | 조치 |",
                  "| --- | --- | --- | --- | --- | --- | --- |"]
        for c in sorted(rows, key=lambda x: (ORDER[x["severity"]], x["id"])):
            severity = SEVERITY[c["severity"]]
            if c.get("clears") == "latched":
                severity += " · 유지"
            actions = " ; ".join(c["actions"]) if c["actions"] else "—"
            cause = c["cause"].replace("|", r"\|")
            lines.append(f"| `{c['code']}` | {c['id']} | {severity} | {c['origin']} "
                         f"| {c['title']} | {cause} | {actions} |")
        lines.append("")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="다시 만들지 않고 어긋났는지만 본다")
    args = ap.parse_args()

    catalog = json.loads(CATALOG.read_text(encoding="utf-8"))
    rendered = render(catalog)

    if args.check:
        current = OUTPUT.read_text(encoding="utf-8") if OUTPUT.exists() else ""
        if current != rendered:
            print(f"{OUTPUT} 가 카탈로그와 다르다. "
                  "python3 tools/gen_alarm_reference.py 로 다시 만들 것.",
                  file=sys.stderr)
            return 1
        print(f"{OUTPUT} 최신 ({len(catalog['codes'])} 개)")
        return 0

    OUTPUT.write_text(rendered, encoding="utf-8")
    print(f"{OUTPUT} 생성 ({len(catalog['codes'])} 개)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
