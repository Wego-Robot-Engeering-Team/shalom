# 고객 릴리스 배포 설계

이 디렉터리는 고객에게 전달할 Shalom 릴리스의 **구성 기준**을 둔다. 개발 소스
저장소, 개발용 `build/`·`install/`, 시뮬레이터는 고객 릴리스에 포함하지 않는다.

자동 릴리스 구현 전에 필요한 GitHub runner·서명키·빌드 장비·권한 설정은
[SETUP.md](SETUP.md)에 정리한다.

목표는 다음 세 가지다.

- Wego 구현 코드는 실행 파일·공유 라이브러리·패키지 형태로 제공한다.
- 고객이 운용·연동에 필요한 설정, 문서, SDK는 명확한 공개 경계로 제공한다.
- 오픈소스와 벤더 구성요소의 재배포 조건·고지를 누락하지 않는다.

## 릴리스 단위

한 버전의 전달 매체는 다음 하나의 최상위 디렉터리로 만든다. 실제 전달 시 이
디렉터리를 `shalom-release-<version>.tar.zst`로 압축한다.

```text
shalom-release-0.1.0/
├── RELEASE.md                 # 버전, 대상 장비, 변경 사항, 알려진 제한
├── manifest.json              # 파일 hash, Git/submodule commit, 빌드 환경, SBOM 참조
├── checksums.txt              # 전달 파일 SHA-256
├── LICENSES/
│   ├── INDEX.md               # 구성요소·버전·라이선스·출처 대응표
│   ├── bundled/               # runtime에 함께 빌드한 제3자 고지
│   ├── system/                # apt .deb의 Debian copyright 고지
│   └── texts/                 # Apache, BSD, LGPL, EPL 등 라이선스 전문
├── robot/                     # Jetson Orin AGX, Ubuntu 24.04, arm64
│   ├── install.sh
│   ├── packages/
│   └── site-config/
├── client-linux-x86_64/       # Ubuntu 24.04 관제 PC용 HMI와 SDK
│   ├── hmi/
│   └── sdk/
└── client-windows-x86_64/     # Windows 관제 PC용 HMI와 SDK
    ├── hmi/
    └── sdk/
```

`manifest.json`에는 최소한 다음을 기록한다.

```json
{
  "release_version": "0.1.0",
  "robot_target": "Jetson Orin AGX / Ubuntu 24.04 / arm64",
  "ros_distro": "Jazzy",
  "source_commit": "<shalom commit>",
  "submodules": {"<path>": "<commit>"},
  "build": {"compiler": "<version>", "build_type": "Release"}
}
```

## 1. 로봇 런타임

로봇 릴리스는 개발 워크스페이스가 아니라 arm64에서 새로 만든 패키지와 오프라인
의존 `.deb` 파일로 구성한다. `--symlink-install` 결과물은 소스 경로를 가리키므로
납품물로 사용하지 않는다.

```text
robot/
├── install.sh
├── packages/
│   ├── shalom-runtime_0.1.0_arm64.deb
│   ├── ros-jazzy-*.deb
│   ├── libboost-*.deb
│   ├── libyaml-cpp*.deb
│   └── 기타 실제 arm64 runtime 의존 .deb
└── site-config/
    ├── robot.env
    ├── platform/b2.yaml
    ├── sensing/xt32.yaml
    ├── sensing/aurora.yaml
    ├── arm/fr3.yaml
    ├── navigation.yaml
    ├── control.yaml
    └── gateway.yaml
```

### `shalom-runtime`에 포함하는 것

- Wego ROS 2 노드 실행 파일: control, navigation, mission, gateway, HMI bridge
- B2·XT32·Aurora·FR3에 필요한 **승인된** 런타임 어댑터와 라이브러리
- Wego가 빌드한 제3자 ROS 패키지의 실행 파일·`.so`·ROS resource index
- 실행에 필요한 launch, `package.xml`, plugin XML, systemd `.service`
- 내부 기본 설정과 버전 정보

시뮬레이터, VLP-16, MuJoCo, RL 학습 자산, 테스트 실행 파일 및 소스는
납품 프리셋에서 제외한다.

systemd unit을 runtime 패키지에 포함하는 것과 개발 PC에서 서비스를 등록하는 것은
별개다. `shalom-robot.service`는 실제 Jetson 제어기 또는 격리된 대상 검증 환경에서만
설치·enable한다. 개발 PC에서는 같은 ROS graph를 수동 launch하며, 자동 시작 서비스가
개발 중인 노드와 중복 실행되지 않도록 systemd 등록을 하지 않는다.

제어기의 프로세스 감독은 중앙 ROS Watchdog 노드가 아니라 systemd를 기준으로
한다. 비정상 종료는 `Restart=` 정책으로 복구하고, 내부 event loop hang까지
감지해야 하는 서비스는 제어기 도입 시 `WatchdogSec=`와 `sd_notify()` IPC
heartbeat를 적용한다. 빠른 로봇 정지는 이 재시작 경로가 아니라 Safety Gate와
Driver의 로컬 timeout이 담당한다.

Jetson의 hardware watchdog은 `/dev/watchdog` 지원과 재부팅 동작을 실물 검증한
뒤 systemd `RuntimeWatchdogSec=`로 활성화한다. 동일 장치를 별도 `watchdog`
daemon과 systemd가 동시에 소유하지 않도록 배포 정책을 하나로 고정한다.

### apt 오프라인 의존성

`packages/`에는 apt가 제공하는 실제 runtime `.deb`를 함께 넣고 `install.sh`가
의존 순서에 맞게 설치한다. 최소 목록은 빌드 시 `rosdep`, `dpkg-query`, 실제
ELF 의존성 검사 결과로 생성한다. 현재 제어 경로에는 다음도 포함해야 한다.

- `ros-jazzy-rmw-cyclonedds-cpp`, `ros-jazzy-cyclonedds`
- `ros-jazzy-navigation2`, `ros-jazzy-nav2-bringup`
- `ros-jazzy-slam-toolbox`, `ros-jazzy-pointcloud-to-laserscan`
- `ros-jazzy-cv-bridge`, `ros-jazzy-twist-mux`

수동으로 추측한 목록만으로 출하하지 않는다. arm64 빌드·설치 검증 환경에서
`install.sh`를 실행한 뒤 실제 bringup까지 확인한다.

### 설정·현장 데이터의 소유 경계

| 위치 | 소유자 | 내용 | 업데이트 정책 |
|---|---|---|---|
| `/opt/shalom/releases/<version>/` | Wego | 실행 파일, launch, 내부 기본값 | 런타임 버전 교체 |
| `/etc/shalom/` | 현장·운영자 | 센서 주소, 보정, 안전 운용값, gateway 정책 | 기존 값을 덮어쓰지 않음 |
| `/var/lib/shalom/` | 로봇 | 지도, waypoint, location, marker, 촬영 spool | 런타임 패키지와 분리 보존 |
| `/var/log/shalom/` | 로봇 | 서비스 로그·진단 로그 | 보존·순환 정책 적용 |

`site-config/`는 설치 시 `/etc/shalom/`에 최초 복사한다. 이후 업그레이드는
conffile 정책 또는 명시적 migration으로 처리하며, 고객이 보정한 YAML을 자동으로
덮어쓰지 않는다. 인증서·비밀번호·private key는 YAML에 넣지 않고 root 전용 별도
파일(`0600`)로 둔다.

YAML을 실행 파일로 바꾸거나 난독화해도 root 권한 사용자가 ROS parameter, 노드
그래프, 메모리를 통해 운용 정보를 알아낼 수 있다. 따라서 설정 파일은 완전한
비밀로 취급하지 않고, 비밀값만 별도 권한 파일로 분리한다.

## 2. 관제 HMI

HMI는 로봇 런타임과 독립된 고객 PC 실행 번들이다. 고객은 압축을 풀고 실행 파일만
실행한다.

```text
hmi/
├── bin/                       # inspection_hmi(.exe)와 OS별 필수 runtime
├── plugins/                   # Qt platform/imageformat/style/tls plugin
├── share/inspection_hmi/      # 진단 코드 등 읽기 전용 자료
├── licenses/                  # Qt 및 번들 구성요소 고지
├── VERSION
├── README
└── run.sh 또는 run.bat
```

- Windows는 `inspection_hmi.exe`, Qt `.dll`, `platforms/qwindows.dll` 등을
  함께 제공한다.
- Linux는 `inspection_hmi`, 필요한 Qt `.so`, `platforms/libqxcb.so` 및 해당
  plugin 의존성을 함께 검증한다.
- 현재 HMI의 설정 저장소는 사용자별 QSettings INI 경로다. `inspection.ini`를
  실행 파일 옆 고정 파일로 납품하려면 HMI 설정 저장 방식을 별도로 변경해야 한다.
- Qt는 동적 링크를 유지한다. Qt 라이브러리 전문·저작권 고지·사용 버전·수정 Qt
  소스 제공 방식 및 수정 라이브러리 설치 안내를 `licenses/`와 README에 포함한다.

HMI의 `hmi_core`는 내부 정적 라이브러리이며 고객 SDK로 제공하지 않는다.

## 3. 고객 연동 SDK

SDK는 HMI와 별도 제품이다. 고객 프로그램은 ROS 2 라이브러리를 링크하지 않고
로봇의 공개 TCP protocol을 사용한다.

```text
sdk/
├── include/shalom/            # api.hpp, client.hpp, export.hpp, types.hpp
├── lib/                       # Linux: libshalom_sdk.so + CMake package
├── bin/                       # Windows: shalom_sdk.dll
├── samples/                   # C++ 및 Python 예제
├── docs/                      # API, transport, state, command, error, security
├── LICENSE
├── NOTICE
└── VERSION
```

- Windows에는 `lib/shalom_sdk.lib` import library도 함께 제공한다.
- Linux에는 `.dll`·`.lib`가 아니라 `lib/libshalom_sdk.so`를 제공한다.
- Python SDK는 소스 형태로 제공된다. Python 구현 자체를 비공개 자산으로 취급하지
  않는다.
- 현재 고객 SDK에는 UDP manual teleop을 제공하지 않는다. 수동 주행은 승인된 HMI
  전용 경로이며, SDK sample도 state 조회·정의된 API 검증 중심으로 둔다.
- 현재 bridge는 TCP client 한 대만 허용한다. HMI와 고객 SDK의 동시 접속을 허용할지,
  혹은 하나만 선택하게 할지를 릴리스 전에 명시적으로 결정한다.

## 4. 공개·비공개 경계

| 구분 | 고객에게 제공 | 제공하지 않음 |
|---|---|---|
| 로봇 | `.deb`, 필요한 `.so`, launch/resource, 설정 템플릿, 로그·지도 운용 데이터 | Wego C/C++ 소스, 개발 build tree, 테스트·시뮬레이션 자산 |
| HMI | 실행 파일, Qt runtime, plugin, 사용 안내·라이선스 | HMI C++ 소스, `hmi_core` 정적 라이브러리 |
| SDK | 공개 헤더, SDK shared library, Python client, sample, API 문서 | SDK C++ 구현 소스, 내부 transport 구현 세부 |
| 임치본 | 계약·법적 요구가 있을 때 전체 소스와 재현 환경 | 고객 일반 납품본에 포함하지 않음 |

`strip`은 디버그 정보와 일반 심볼을 줄이는 릴리스 처리일 뿐, 동적 라이브러리의
공개 API·ROS 토픽·파라미터를 숨기는 보안 기능은 아니다. SDK shared library는
hidden visibility를 기본으로 하고 공개 API만 export한다.

## 5. 제3자 라이선스 기준

| 구성요소 | 라이선스·상태 | 릴리스 처리 |
|---|---|---|
| kiss_icp | MIT | LICENSE 고지 |
| ground_segmentation, ground_segmentation_ros2 | BSD-3-Clause | LICENSE 고지 |
| nav2_ground_consistency_costmap_plugin | BSD-3-Clause | LICENSE 고지 |
| hesai_lidar_ros2 | BSD 계열 | LICENSE 고지, 사용 빌드 옵션 기록 |
| b2_driver + Unitree SDK/ROS2 | Wego 코드 + Apache-2.0/BSD-3 구성요소 | Wego LICENSE, Unitree LICENSE·NOTICE 분리 고지 |
| slamware_ros_sdk | BSD-2-Clause | LICENSE 고지 |
| aurora_remote_public | 재배포 허락 보관 필요 | 서면 허락과 바이너리 내부 제3자 고지 포함 후에만 제공 |
| frcobot_ros2 | 재배포 조건 미확정 | 조건 확정 전 runtime에서 제외 |
| fairino-cpp-sdk | 도입·라이선스 확인 필요 | 실제 채택 후 버전·LICENSE·NOTICE 추가 |
| ROS 2 Jazzy/Nav2 | Apache-2.0 중심 | apt copyright 자동 수집 |
| slam_toolbox | LGPL-2.1 | 전문, 저작권 고지, 대응 소스 제공 방식 |
| CycloneDDS | EPL-2.0 / EDL-1.0 | 전문·저작권 고지 |

모든 납품 구성요소는 `LICENSES/INDEX.md`와 `manifest.json`에 버전·출처·SHA-256을
한 번씩 기록한다. 라이선스 수집 실패는 릴리스 실패로 처리한다.

## 6. 릴리스 빌드와 검증

릴리스는 개발자의 기존 `build/`, `install/`, `log/`에서 만들지 않는다.

1. 고정한 Shalom commit과 모든 submodule commit을 checkout한다.
2. Jetson arm64 / Ubuntu 24.04 / ROS 2 Jazzy의 깨끗한 빌드 환경에서 Release로 빌드한다.
3. 허용 목록 기반으로 로봇 runtime install tree를 만들고 `.deb`를 생성한다.
4. apt runtime `.deb`, `LICENSES`, site-config template, 설치 스크립트를 조립한다.
5. Windows와 Linux에서 HMI·SDK를 각각 빌드하고 압축 해제 환경에서 실행한다.
6. SDK 패키지만으로 C++·Python sample이 빌드·연결되는지 검증한다.
7. 에어갭 Jetson에서 `install.sh` 후 `robot_bringup`을, 개발 PC에서
   `simulation_bringup`을 각각 기동 검증한다.
8. checksums, manifest, SBOM, 서명과 `RELEASE.md`를 생성한다.

## 7. 출시 차단 항목

아래는 자동 빌드가 성공해도 고객 릴리스를 만들면 안 되는 항목이다.

- Aurora SDK 바이너리의 재배포 서면 허락 또는 대체 배포 절차 미확정
- FAIRINO ROS/SDK·형상 자산의 재배포 근거 미확정
- HMI와 고객 SDK 동시 접속·명령 권한 정책 미확정
- arm64 오프라인 설치 및 실기 bringup 미검증
- LICENSES/SBOM/manifest/checksum 미생성
- Qt LGPL 고지, 해당 Qt 소스 제공 방식, 수정 라이브러리 설치 안내 미포함

## 현재 구현 범위

로봇 runtime과 site-config의 첫 기반은 추가되어 있다.
`scripts/build_robot_runtime.sh`는 Jetson arm64에서 별도 Release install tree를 만들고
`shalom-runtime_<version>_arm64.deb`를 생성한다.
`scripts/build_site_config.sh`는 업데이트가 현장 설정을 덮어쓰지 않는 conffile 기반
`shalom-site-config_<version>_all.deb`를 생성한다.

`scripts/build_robot_release.sh`는 위 두 패키지와 apt runtime 의존 `.deb`를 수집해
다음 파일을 만든다.

```text
dist/shalom-release-<version>-arm64.tar.zst
└── shalom-release-<version>/
    ├── robot/install.sh
    ├── robot/packages/*.deb
    ├── robot/site-config/robot.env.example
    ├── LICENSES/
    ├── manifest.json
    ├── checksums.txt
    └── RELEASE.md
```

`collect_licenses.sh`는 선언한 서브모듈 LICENSE/NOTICE와 실제 apt `.deb` 안의
Debian copyright를 수집한다. 하나라도 선언된 고지가 없으면 번들 생성을 실패시킨다.
`build_sdk_linux_release.sh`는 Linux x86_64에서 공개 C++ `.so`·헤더·CMake package,
Python client, C++/Python sample, API 문서, LICENSE를 묶은
`shalom-sdk-<version>-linux-x86_64.tar.zst`를 만든다. 이 스크립트는 압축 파일 안의
C++ sample을 다시 빌드해 고객 SDK 단독 사용을 확인한다. Windows HMI/SDK release,
SBOM 생성은 아직 구현 전이다.

`build_hmi_linux_release.sh`는 Linux x86_64에서 HMI 실행 파일, 필요한 Qt6 `.so`,
X11 platform·image·TLS plugin, 진단 자료, 라이선스를
`inspection-hmi-<version>-linux-x86_64.tar.zst`로 묶는다. HMI 번들에는 SDK 헤더나
라이브러리를 포함하지 않는다.

로봇 패키지 생성 예시는 다음과 같다.

```bash
./deploy/scripts/build_robot_release.sh --version 0.1.0
./deploy/scripts/build_sdk_linux_release.sh --version 0.2.0
./deploy/scripts/build_hmi_linux_release.sh --version 0.1.0
```

이 명령은 arm64 Jetson release runner와 sudo apt 권한이 필요하다. 고객 로봇이 아닌
별도 빌드 Jetson에서만 실행한다. SDK 명령은 Linux x86_64 runner에서 실행한다.
