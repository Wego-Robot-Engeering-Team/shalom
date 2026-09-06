# 철도차량 하부점검 관제 시스템 (GCS)

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

프리셋을 쓴다. 옵션을 손으로 넘기면 클린 빌드 때마다 빠뜨리게 된다.

```bash
cd gcs
cmake --preset dev          # 개발용
cmake --build --preset dev
ctest --preset dev
```

| 프리셋 | 로그인 | 빌드 타입 | 출력 |
|---|---|---|---|
| `dev` | **생략** | Debug | `build/` |
| `release` | **필수** | Release | `build-release/` |

납품 빌드는 `release` 프리셋을 쓴다.

```bash
cmake --preset release && cmake --build --preset release
```

Qt 경로는 프리셋에 Homebrew 기본 위치가 들어 있다. Ubuntu 에서 다른 곳에
설치했다면 `-DCMAKE_PREFIX_PATH=<경로>` 를 덧붙인다.

### 빌드 옵션

| 옵션 | 기본 | 설명 |
|---|---|---|
| `GCS_REQUIRE_LOGIN` | `ON` | 기동 시 조작자 로그인 요구 |
| `GCS_BUILD_TESTS` | `ON` | 단위 테스트 빌드 |

⚠️ 로그인 생략은 **런타임 플래그가 아니라 빌드 옵션이다.** 실행 인자로 끌 수
있게 두면 납품 빌드에서도 꺼진 채 나갈 수 있고 아무도 눈치채지 못한다.
생략한 빌드는 상단바에 "개발 빌드" 로 표시되고, CMake 구성 시 경고가 나오며,
조작 이력에도 실제 조작자가 아님이 드러나는 이름으로 남는다.

## 실행

```bash
./build/inspection_gcs
```

최초 실행에서는 관리자 비밀번호를 설정한다. 이후에는 로그인 화면에서 이름과
권한을 선택하고 시작한다. 여기서 입력한 이름이 이후 모든 권한 동작의 이력에
기록되므로, 조작 이력이 감사 추적으로 쓰인다.

기본은 **내장 시뮬레이터**다. 명령을 실제로 처리한다 — 비상정지가 실제로
정지시키고, 수동 조작으로 로봇이 움직이며, 지도에서 목표를 찍으면 그리로 주행한다.

`--live` 를 주면 설정된 주소의 브릿지에 raw TCP 로 접속한다.

```bash
./build/inspection_gcs --live
```

둘 다 `RobotLink` 인터페이스를 구현하므로 화면은 어느 쪽에 붙었는지 알지 못한다.
브릿지 주소는 설정 창의 연결 탭에서 바꾼다.

### 화면 구성

지도·비상정지·배터리·이벤트 로그는 어느 화면에서도 사라지지 않는다. 다른 것을
조작하는 동안 로봇 위치를 놓치는 상황을 만들지 않기 위한 배치다. 좌측 네비게이션은
우측 컨텍스트 열만 바꾼다.

| 항목 | 내용 |
|---|---|
| 주행 | 상태 모니터링, 수동 조작(수동 모드에서만 표시) |
| 위치 | 점검포인트·충전 스테이션·시작 위치 교시 및 편집 |
| 로봇팔 | FR3 3D 자세, 조작성 지수, 7축 슬라이더, EE 목표, 프리셋 |
| 촬영 | 촬영 트리거, 2D/3D 미리보기, 메타데이터 입력·저장 |
| 진단 | 센서 상태, 로봇 연결 지표, 촬영 데이터 전송 현황 |
| 이력 | 저장 장치의 과거 촬영 조회·내려받기 |

### 개발용 플래그

납품 빌드에서는 제거한다.

| 플래그 | 설명 |
|---|---|
| `--dark` | 다크 테마로 시작 |
| `--no-login` | 로그인 생략 |
| `--view <이름>` | 시작 화면 지정 (`drive` `locations` `arm` `capture` `diagnostics` `data` `events`) |
| `--live` | 로봇 브릿지에 접속 (기본은 내장 시뮬레이터) |
| `--host <주소>` `--port <포트>` | `--live` 접속 대상 override |
| `--manual` | 수동 모드로 시작 (조작 패널 확인용) |
| `--size <W>x<H>` | 창 크기 지정. 좁은 화면 배치 확인용 |
| `--shot <경로>` | 창을 캡처하고 종료 |
| `--shot-dialog <종류> <경로>` | 대화상자 캡처 (`welcome`, `settings`, `settings:1`, `notifications`) |

```bash
./build/inspection_gcs --dark --view arm
./build/inspection_gcs --no-login --shot /tmp/screen.png
```

## 테스트

```bash
ctest --test-dir build --output-on-failure
```

| 스위트 | 검증 대상 |
|---|---|
| `framing` | TCP 프레임 경계 복원. 부분·병합 수신, 정합 손상, 길이 가드 |
| `mapinfo` | map 프레임 ↔ 씬 좌표 변환과 회전 부호 |
| `headers` | 헤더 위생. 네임스페이스 내 Qt 전방 선언 금지, 헤더 영문 유지 |
| `catalog` | 진단 코드 카탈로그. 누락·중복, 프로토콜 코드와의 드리프트 |
| `theme` | 스타일시트 생성. 토큰 치환, 색상 규정 준수 |
| `sim` | **안전 동작**. 비상정지 즉시 정지·해제 시 돌진 방지, 데드맨, 수동 우선권 |
| `bridge` | 가짜 브릿지 서버 상대 실통신. 구독·하트비트·재연결·스트림 손상·응답 누락 |
| `capture` | 위치 등록 검증. 이동 중·정보 노후 차단, 신뢰도 저하 경고 |
| `session` | 인증. PBKDF2 표준 벡터, salt, 평문 미저장, 잠금 |
| `capture_meta` | 촬영 메타데이터. 규정 파일명, 필수 항목 누락 판정 |
| `records` | 이력 파일명 역파싱, 공유 폴더 스캔 |
| `docs` | 명세서와 코드의 드리프트. 채널·오류 코드·프레이밍 상수 |

`sim` 스위트는 과업지시서 2.2.5 안전요구사항을 코드로 못박은 것이다. 나중에
`BridgeClient` 가 시뮬레이터를 대체할 때 그대로 인수 기준이 된다.

---

## 디렉터리

```
src/
  theme/     디자인 토큰(다크·라이트), 전역 스타일시트, UI 배율
  net/       프레이밍, JSON 봉투, 채널 이름, 브릿지 클라이언트
  mapview/   map 프레임 좌표 변환, 지도 아이템, 점유격자 렌더러
  diag/      진단 코드 카탈로그, 이벤트 로그, 코드 상세 팝업
  auth/      세션과 권한 (운용자 / 관리자)
  robot/     RobotLink 인터페이스 — 시뮬레이터와 브릿지의 공통 계약
  sim/       내장 시뮬레이터
  widgets/   공용 위젯, 계측 위젯, 3D 자세 뷰, 네비게이션 레일
  panels/    화면 패널
  views/     로그인·설정 창
resources/
  error_codes.json   진단 코드 단일 출처
  brand/             발주기관 로고 (README 참조)
tests/
```

## 납품 패키지

```bash
cmake --preset release
cmake --build --preset release
cmake --install build-release --prefix dist/inspection-gcs
```

고객에게는 **소스가 아니라 이 폴더 하나**를 준다. 압축을 풀고 실행하면
끝나야 하고, 지우면 흔적이 남지 않아야 한다.

**Windows**

```
INSPECTION-GCS-1.0.0-win64/
├── inspection_gcs.exe
├── inspection.ini                  설정 (아래 "설정값 저장 위치" 참조)
├── Qt6Core.dll  Qt6Gui.dll  Qt6Widgets.dll  Qt6Network.dll  Qt6Svg.dll
├── platforms/qwindows.dll      Qt 플랫폼 플러그인 — 없으면 실행되지 않는다
├── styles/  imageformats/  iconengines/
├── include/inspection/             공개 헤더 7종 — 인터페이스 계약만
├── share/
│   ├── error_codes.json        진단 코드 카탈로그
│   └── bridge_protocol.md      통신 규약
├── licenses/                   Qt LGPL v3 · Pretendard OFL · Qt 소스 취득 안내
├── VERSION.txt                 버전 · 커밋 해시 · Qt 버전 (자동 생성)
└── README.txt                  설치 · 실행 · 설정 변경 방법
```

**Ubuntu**

```
INSPECTION-GCS-1.0.0-linux-x86_64/
├── inspection_gcs.sh               실행 스크립트 (LD_LIBRARY_PATH 지정)
├── bin/inspection_gcs
├── lib/                        libQt6*.so.6 · libgcs_core.so
├── plugins/platforms/libqxcb.so
├── inspection.ini
├── include/  share/  licenses/  VERSION.txt  README.txt
```

`RPATH` 를 `$ORIGIN/../lib` 로 박아 두므로 스크립트 없이도 돌지만, 고객
환경의 시스템 Qt 와 섞이지 않도록 스크립트를 함께 준다.

**인스톨러는 만들지 않는다.** 에어갭 환경에서 인스톨러는 관리자 권한과
레지스트리 문제를 만든다. 압축을 풀어 쓰는 편이 낫고, 문제가 생기면 폴더를
지우는 것으로 되돌릴 수 있다.

### 설정값 저장 위치

> **결정 필요.** 현재 구현은 `QSettings` 기본 위치를 쓴다. 납품에는 맞지 않다.

| | 현재 (기본 위치) | 권장 (`inspection.ini`) |
|---|---|---|
| Windows | 레지스트리 `HKCU\Software\WEGO Robotics\Inspection GCS` | 실행 파일 옆 |
| Ubuntu | `~/.config/WEGO Robotics/Inspection GCS.conf` | 실행 파일 옆 |
| 범위 | **로그인 계정별** | PC 단위로 하나 |
| 초기값 납품 | 불가 | 파일을 채워서 납품 |
| 백업·이관 | 어려움 | 파일 복사 |

현재 방식의 문제는 관제 PC 를 여러 명이 쓸 때 드러난다. 계정마다 로봇 주소가
따로 저장되고, **관리자 비밀번호 해시도 계정별로 저장되어** 다른 계정으로
로그인하면 비밀번호가 초기화된 것처럼 보인다.

저장되는 값: 로봇 주소·포트, 테마, 글자 크기, 수동 조작 기본 속도,
이벤트 로그 폴더·보관 기간, 촬영 저장 장치 경로, 관리자 비밀번호 해시·솔트.

**저장되지 않는 것**: 점검포인트와 충전 스테이션 위치. 이것들은 로봇이
가지고 있고 관제는 받아서 보여줄 뿐이다. 관제 PC 를 바꿔도 따라오지 않는다.

공개 헤더에는 **인터페이스 계약에 해당하는 것만** 들어간다 — 통신 규약, 로봇
연동 인터페이스, 진단 코드, 메타데이터 규약. 화면 구성이나 시뮬레이터 같은
구현 세부는 고객이 알 필요도 의존할 이유도 없다.

`VERSION.txt` 의 커밋 해시에 `-dirty` 가 붙어 있으면 커밋되지 않은 변경이
섞인 빌드다. 그런 빌드는 소프트웨어 임치본과 산출물이 달라지므로 납품하지
않는다.

절차와 확인 목록은 [packaging/README.md](packaging/README.md) 참조.

## 주석 언어 규약

- **헤더(`.h`)는 영문.** 고객 배포 시 API 레퍼런스로 읽힌다.
- **구현(`.cpp`)은 한국어.** 과업지시서 7.3 임치 요건이 "모든 소스 파일,
  한국어 주석 포함"을 명시한다.

## 알림

조작자가 조치해야 하는 사건(주의 등급 이상)은 화면 하단에 알림으로 뜬다.
**모달 대화상자가 아니다.** 모달은 포커스를 가져가고 화면을 가리는데,
절대 가려지거나 막혀서는 안 되는 것이 비상정지 버튼이다. 정지를 막는 경고는
경고가 없느니만 못하다.

같은 이유로 알림은 비상정지가 있는 우상단을 피해 하단에 배치하고,
키보드 포커스를 가져가지 않는다.

## 권한

| 역할 | 가능한 동작 |
|---|---|
| 운용자 | 주행, 촬영, 미션 실행 |
| 관리자 | 위 항목 + **비상정지 해제**, 위치 교시·삭제, 설정 변경 |

비상정지 **발동에는 어떤 인증도 걸지 않는다.** 급할 때 인증하다 못 누르는 상황을
만들면 안 되기 때문이다. 해제에만 관리자 비밀번호를 요구한다.

이 비밀번호는 오조작 방지를 위한 **운용 통제 수단이지 보안 경계가 아니다.**
관제 PC에 파일 접근이 가능한 사람은 우회할 수 있으며, 비상정지의 최종 권한은
하드웨어 버튼과 로봇측 안전 노드에 있다.

## 브랜드 자산

`resources/brand/` 에 발주기관 로고를 넣는다. 자산이 없으면 중립 마크로
폴백한다. 상표 사용 승인 절차는 `resources/brand/README.md` 참조.
