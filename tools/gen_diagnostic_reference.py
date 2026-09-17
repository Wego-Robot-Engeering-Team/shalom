#!/usr/bin/env python3
# Copyright (c) 2026 WeGo Robotics. All rights reserved.

"""hmi/resources/error_codes.json 에서 진단·운용 코드 기준 문서를 만든다.

카탈로그가 단일 출처다. 표를 손으로 쓰면 화면이 띄우는 내용과 어긋나고,
어긋난 사실은 검수 자리나 현장에서야 드러난다.

    python3 tools/gen_diagnostic_reference.py          문서를 다시 만든다
    python3 tools/gen_diagnostic_reference.py --check  어긋나면 0 이 아닌 값으로 끝난다
"""

import argparse
import collections
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
CATALOG = ROOT / "hmi/resources/error_codes.json"
OUTPUT = ROOT / "docs/diagnostic_code_reference.md"

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

HEAD = """# 진단·운용 코드 기준

점검 로봇과 관제의 공통 코드 계약 및 운용 조치 기준이다. 고객 UI, 브릿지 응답,
이벤트 로그, 운용 매뉴얼은 이 카탈로그를 같은 의미로 사용한다.

이 기준은 제품이 이해할 수 있는 코드 어휘 전체를 정의한다. 실제 설치본에서 발행되는
코드는 탑재 센서·옵션·활성 기능에 따라 그 일부일 수 있다. 설치하지 않은 기능의 코드가
나타나면 버전·구성 불일치를 먼저 확인한다.

**이 문서는 `hmi/resources/error_codes.json` 에서 생성한다. 직접 고치지 말 것.**
`python3 tools/gen_diagnostic_reference.py` 로 다시 만든다.

## 분류와 우선순위

| 등급 | 뜻 |
| --- | --- |
| 치명 | **알람**. 운용을 중지하고 안전 확인·조치가 필요하다 |
| 오류 | 요청 또는 기능이 실패했다. 원인을 제거한 뒤 명시적으로 재시도한다 |
| 경고 | 기능은 제한되거나 보호 동작이 진행된다. 운용자가 상태를 확인한다 |
| 정보 · 정상 | 운용 이벤트·감사 이력이다. 별도 조치는 필요 없다 |

`E_` 코드는 명령 응답의 `err`에만 실리는 **명령 거절 코드**다. 그 외 코드는
`evt/log`로 발행되는 **진단·운용 이벤트**다. 따라서 모든 오류 코드가 알람은 아니며,
모든 이벤트가 조치를 요구하지도 않는다.

해제 방식은 등급과 독립적이다. `유지`는 원인이 사라져도 화면에 남으며 사람이
확인·해제해야 한다. `짝 해제`는 지정된 정상/해제 이벤트가 오면 사라지고, `1회`는
기록만 남긴다.

코드 번호 앞자리로 영역을 구분한다 — 1 전송·시스템, 2 안전, 3 주행·점검,
4 인식, 5 로봇팔·본체, 6 촬영, 7 전원, 8 조작 이력.

## 릴리스 검증

카탈로그를 수정한 릴리스는 Markdown과 Excel을 모두 다시 생성하고 검증한다.

```bash
python3 tools/gen_diagnostic_reference.py
python3 tools/gen_diagnostic_reference.py --check
python3 -m pip install -r tools/requirements-docs.txt
python3 tools/gen_diagnostic_reference_xlsx.py
python3 tools/gen_diagnostic_reference_xlsx.py --check
```
"""


def clear_label(entry):
    return {"latched": "유지", "clears_with": "짝 해제"}.get(
        entry.get("clears"), "1회")


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
                  "| 코드 | 번호 | 등급 | 해제 | 발생처 | 관련 채널 | 메시지 | 원인 | 조치 |",
                  "| --- | --- | --- | --- | --- | --- | --- | --- |"]
        for c in sorted(rows, key=lambda x: (ORDER[x["severity"]], x["id"])):
            severity = SEVERITY[c["severity"]]
            actions = " ; ".join(c["actions"]) if c["actions"] else "—"
            cause = c["cause"].replace("|", r"\|")
            channel = c.get("channel") or "—"
            lines.append(f"| `{c['code']}` | {c['id']} | {severity} | {clear_label(c)} "
                         f"| {c['origin']} | `{channel}` | {c['title']} | {cause} | {actions} |")
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
                  "python3 tools/gen_diagnostic_reference.py 로 다시 만들 것.",
                  file=sys.stderr)
            return 1
        print(f"{OUTPUT} 최신 ({len(catalog['codes'])} 개)")
        return 0

    OUTPUT.write_text(rendered, encoding="utf-8")
    print(f"{OUTPUT} 생성 ({len(catalog['codes'])} 개)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
