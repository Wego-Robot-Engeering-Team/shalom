"""디자인 토큰 — 다크/라이트 두 팔레트와 런타임 전환.

설계 방침
---------
관제 소프트웨어는 오래 들여다보는 도구다. 화려함보다 밀도와 가독성이 우선이고,
장식 요소가 상태 정보와 경쟁하면 안 된다. 그래서:

  - 그림자·글로우 없음. 면 구분은 hairline 경계선으로만 한다.
  - 라운드는 작게(4~6px). 과한 곡률은 도구가 아니라 위젯처럼 보인다.
  - 액센트는 절제된 청색 하나. 의미 색(성공/경고/위험)은 채도를 낮춰
    상태 표시가 필요할 때만 눈에 들어오게 한다.
  - 수치는 전부 등폭 폰트. 자릿수가 흔들리면 값 변화를 못 읽는다.

상태 색 4종(완료/현재/미완료/오류)은 과업지시서 2.2.7 [1] ② 규정이다.
색상값 자체는 테마별로 조정하되 의미 대응은 바꾸지 말 것 — 검수 항목이다.
"""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class Palette:
    name: str

    # 배경 계층
    bg: str
    surface: str
    surface_hi: str
    surface_hover: str
    overlay: str          # 지도 위 반투명 칩 배경 (알파 별도 부여)

    # 경계선
    border: str
    border_hi: str

    # 텍스트
    text: str
    text_dim: str
    text_mute: str
    text_on_accent: str

    # 액센트 / 의미 색
    accent: str
    accent_hi: str
    accent_lo: str
    success: str
    warning: str
    danger: str
    danger_hi: str
    danger_lo: str

    # 과업지시서 규정 상태 색
    wp_done: str
    wp_current: str
    wp_todo: str
    wp_error: str

    # 지도 레이어
    map_free: str
    map_occupied: str
    map_unknown: str
    plan: str
    trail: str
    tag: str

    @property
    def is_dark(self) -> bool:
        return self.name == "dark"


DARK = Palette(
    name="dark",
    bg="#101317",
    surface="#161A1F",
    surface_hi="#1D2228",
    surface_hover="#252B32",
    overlay="#0C0F12",

    border="#252A31",
    border_hi="#333A43",

    text="#E3E6EA",
    text_dim="#98A0AA",
    text_mute="#69727C",
    text_on_accent="#FFFFFF",

    accent="#4A8FE7",
    accent_hi="#66A2EF",
    accent_lo="#3A76C4",

    success="#3FA46A",
    warning="#D2963C",
    danger="#DC5B53",
    danger_hi="#E87068",
    danger_lo="#B4453F",

    wp_done="#3FA46A",
    wp_current="#4A8FE7",
    wp_todo="#69727C",
    wp_error="#DC5B53",

    map_free="#232830",
    map_occupied="#5B6672",
    map_unknown="#14181C",
    plan="#4A8FE7",
    trail="#7B858F",
    tag="#D2963C",
)

LIGHT = Palette(
    name="light",
    bg="#F4F5F7",
    surface="#FFFFFF",
    surface_hi="#F0F2F5",
    surface_hover="#E6E9ED",
    overlay="#FFFFFF",

    border="#E1E4E9",
    border_hi="#C8CDD4",

    text="#1B1F24",
    text_dim="#586069",
    text_mute="#868E96",
    text_on_accent="#FFFFFF",

    accent="#2C6FD1",
    accent_hi="#3E82E4",
    accent_lo="#245BAC",

    success="#1D8A52",
    warning="#B0741A",
    danger="#C33E36",
    danger_hi="#D24C43",
    danger_lo="#9E2F29",

    wp_done="#1D8A52",
    wp_current="#2C6FD1",
    wp_todo="#868E96",
    wp_error="#C33E36",

    map_free="#FFFFFF",
    map_occupied="#6E7883",
    map_unknown="#E3E6EA",
    plan="#2C6FD1",
    trail="#8A929B",
    tag="#B0741A",
)


@dataclass(frozen=True)
class Metrics:
    """4px 그리드. 간격은 전부 이 배수로만 쓴다."""
    s1: int = 4
    s2: int = 8
    s3: int = 12
    s4: int = 16
    s5: int = 24
    s6: int = 32

    # 라운드는 작게 유지한다 — 과한 곡률은 도구가 아니라 장난감처럼 보인다.
    r_sm: int = 3
    r_md: int = 5
    r_lg: int = 6
    r_full: int = 999

    row_h: int = 28
    ctl_h: int = 30
    ctl_h_sm: int = 24


@dataclass(frozen=True)
class Type:
    """폰트 스택.

    Pretendard 를 assets/fonts 에 번들하면 Windows/Ubuntu 렌더링이 일치한다.
    Windows 기본 한글(맑은 고딕)은 자간·힌팅이 낡아 화면 인상을 크게 떨어뜨리므로
    납품 빌드에는 반드시 번들할 것. Pretendard: SIL OFL 1.1 (번들·재배포 허용)
    """
    ui: str = ('"Pretendard Variable", "Pretendard", "Inter", -apple-system, '
               '"Segoe UI", "Malgun Gothic", sans-serif')
    mono: str = '"JetBrains Mono", "SF Mono", "Consolas", "D2Coding", monospace'

    xs: int = 10
    sm: int = 11
    md: int = 12
    lg: int = 14
    xl: int = 17
    xxl: int = 24


METRICS = Metrics()
TYPE = Type()

# ---- 현재 테마 ---------------------------------------------------------
# QPainter 로 직접 그리는 위젯은 paint 시점에 palette() 를 호출한다.
# 그래야 테마 전환이 재시작 없이 반영된다.
#
# 기본값은 라이트. 검수고 조명 환경과 인쇄 보고서 캡처를 고려한 선택이며,
# 관제실이 어두우면 상단바 토글로 즉시 전환한다.
_current: Palette = LIGHT


def palette() -> Palette:
    return _current


def set_theme(name: str) -> Palette:
    global _current
    _current = LIGHT if name == "light" else DARK
    return _current


def toggle_theme() -> Palette:
    return set_theme("light" if _current.is_dark else "dark")


def mono_family() -> str:
    """QFont(family) 에 넘길 첫 번째 등폭 폰트 이름."""
    return TYPE.mono.split(",")[0].strip().strip('"')
