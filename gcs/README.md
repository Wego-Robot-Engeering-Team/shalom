# SHALOM 관제 (GCS)

Unitree B2 + Franka FR3 기반 철도차량 하부 점검시스템의 관제 UI.
C++ / Qt 6 데스크톱 애플리케이션이며 Windows·Ubuntu 크로스 플랫폼으로 빌드된다.

통신 규약은 `../docs/bridge_protocol.md` 를 따른다. 관제 PC에는 ROS2/DDS를
설치하지 않고, 로봇측 브릿지 노드와 raw TCP 단일 연결로만 통신한다.

---

## 요구사항

| 항목 | 버전 |
|---|---|
| Qt | 6.5 이상 (Core, Gui, Widgets, Network, Svg, Test) |
| CMake | 3.21 이상 |
| 컴파일러 | C++17 (MSVC 2019+, GCC 11+, AppleClang) |

macOS 개발 환경 준비:

```bash
brew install qt ninja
```

## 빌드

```bash
cd gcs
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt
cmake --build build
```

Ubuntu 에서는 `-DCMAKE_PREFIX_PATH` 를 Qt 설치 경로로 바꾸거나 생략한다.

## 실행

```bash
./build/shalom_gcs
```

브릿지가 아직 없으므로 합성 데모 데이터로 구동된다.

### 개발용 플래그

| 플래그 | 설명 |
|---|---|
| `--dark` | 다크 테마로 시작 (기본은 라이트) |
| `--shot <경로>` | 창을 캡처하고 종료. 레이아웃 검증, 현장 지원 시 화면 상태 수집용 |

```bash
./build/shalom_gcs --dark
./build/shalom_gcs --shot /tmp/screen.png
```

테마는 실행 중에도 상단바 우측 버튼으로 전환된다.

## 테스트

```bash
ctest --test-dir build --output-on-failure
```

| 스위트 | 검증 대상 |
|---|---|
| `framing` | TCP 프레임 경계 복원. 부분·병합 수신, 정합 손상, 길이 가드 |
| `mapinfo` | map 프레임 ↔ 씬 좌표 변환과 회전 부호 |
| `catalog` | 진단 코드 카탈로그. 누락·중복·문서 드리프트 |
| `theme` | 스타일시트 생성. 토큰 치환, 색상 규정 준수 |

---

## 디렉터리

```
src/
  theme/     디자인 토큰(다크·라이트)과 전역 스타일시트
  net/       프레이밍, JSON 봉투, 채널 이름
  mapview/   map 프레임 좌표 변환
  diag/      진단 코드 카탈로그, 이벤트 로그, 코드 상세 팝업
  widgets/   Card·Badge 등 공용 위젯, 브랜드 마크
  panels/    화면 패널
resources/
  error_codes.json   진단 코드 단일 출처
  brand/             발주기관 로고 (README 참조)
tests/
```

## 주석 언어 규약

- **헤더(`.h`)는 영문.** 고객 배포 시 API 레퍼런스로 읽힌다.
- **구현(`.cpp`)은 한국어.** 과업지시서 7.3 임치 요건이 "모든 소스 파일,
  한국어 주석 포함"을 명시한다.

## 브랜드 자산

`resources/brand/` 에 발주기관 로고를 넣는다. 자산이 없으면 중립 마크로
폴백한다. 상표 사용 승인 절차는 `resources/brand/README.md` 참조.
