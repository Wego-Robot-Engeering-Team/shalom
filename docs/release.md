# 릴리스

고객에게 제공하는 릴리스 산출물의 구성과, 릴리스 전에 정리해야 하는 것들을 적는다.
설치 절차는 [install.md](install.md), 통신 규약은 [bridge_protocol.md](bridge_protocol.md)
를 본다.

## 산출물 4종

릴리스 산출물은 하나가 아니다. 받는 사람도 쓰임새도 다르므로 따로 만든다.

| | 대상 | 내용 |
|---|---|---|
| 관제 런타임 | 관제 PC (산업용 Windows) | 실행 파일, Qt 런타임, 기본 설정, 라이선스 |
| 로봇 런타임 | 젯슨 | 노드 실행 파일, launch, config |
| 연동 SDK | 고객 개발자 | 공개 헤더, 라이브러리, 예제 |
| 임치본 | 한국저작권위원회 | 전체 소스와 재현 가능한 빌드 환경 |

```text
1. 관제 런타임                        2. 로봇 런타임
   inspection-hmi/                       (봉인 이미지 권장)
   ├─ bin/      실행 파일 + hmi_core     ├─ lib/<pkg>/    노드 실행 파일
   │            + Qt 런타임              ├─ share/<pkg>/  launch, config,
   ├─ share/    error_codes.json         │                package.xml
   ├─ inspection.ini  초기값 채워 출하   ├─ licenses/
   ├─ licenses/ Qt LGPLv3, 폰트 OFL      └─ VERSION.txt
   └─ VERSION.txt

3. 연동 SDK                           4. 임치본
   shalom-sdk/                           ├─ 전체 소스
   ├─ include/inspection_hmi/  헤더 7개  ├─ 서브모듈 고정 커밋
   ├─ include/inspection/  framing.hpp   ├─ ROS·Qt·컴파일러 버전
   ├─ bin/hmi_core.dll                   └─ 빌드 이미지
   ├─ lib/hmi_core.lib
   ├─ lib/cmake/inspection_hmi/
   ├─ samples/
   ├─ protocol.md
   └─ README.md + VERSION.txt
```

Windows 는 DLL 과 import library 를 함께 줘야 예제가 링크된다. Linux 관제 PC
라면 같은 자리에 `libhmi_core.so` 가 온다. 젯슨에는 `hmi_core` 가 없다 —
로봇 런타임은 관제 코어를 링크하지 않는다.

`bin/` 과 `lib/` 배치는 이미 [../hmi/CMakeLists.txt](../hmi/CMakeLists.txt) 의
`install(TARGETS)` 규칙이 그대로 만들어 낸다. `RUNTIME`→`bin`,
`ARCHIVE`→`lib` 이므로 MSVC 에서 `hmi_core.dll` 과 `hmi_core.lib` 가 제자리에
간다. 새로 할 일이 아니다.

## 선행 확인 — 코드로 해결되지 않는다

아래는 포장 방식과 무관하며, 막히면 납품 자체가 멈춘다. 코드 작업보다 먼저
답을 받아야 한다.

### FAIRINO (`frcobot_ros2`)

라이선스가 없다. 원격 브랜치 4개(`main`, `master`, `V2.0`, `V3.0`) 어디에도
`LICENSE` 파일이 없고, README 에도 라이선스 절이 없다. 소스의 유일한 저작권
고지는 ros2_control 템플릿에서 온 `Copyright 2021 ros2_control Development
Team` 이다. 라이선스 미표기는 기본 저작권이 적용되어 재배포 권리가 없다는
뜻이다.

다만 FAIRINO 는 현행 언어 SDK 네 개(`fairino-cpp-sdk`, `-python-sdk`,
`-csharp-sdk`, `-java-sdk`)에 모두 Apache-2.0 을 붙였다. ROS 계열만 빠져 있어
의도적 제한이라기보다 관리가 멈춘 저장소로 보인다. **같은 조건(Apache-2.0)
적용을 요청하면 선례가 있어 받아들여질 여지가 있다.**

총판 사이트의 "Every Fairino SDK is free and license-free" 는 무상이라는
뜻이지 저작권 허락이 아니고, 저작권자 본인의 고지도 아니다. 근거로 쓰지 않는다.

**패키지 자체는 빼면 된다.** `frcobot_ros2` 는 우리 어떤 패키지의 빌드·실행
의존도 아니다. 팔은 표준 `sensor_msgs/JointState` 를 `fr3/joint_command` 로
발행해 제어하며, fairino 노드를 띄우는 launch 가 없다. 납품 프리셋에서
제외해도 아무것도 깨지지 않는다.

**남는 것은 형상 데이터다.** `hmi/resources/robot_mesh.bin` 이 FAIRINO 의
`fairino3_v6/*.STL` 에서 만들어진 파생 저작물이고 납품물에 설치된다. 정점
클러스터링으로 1.6만 삼각형까지 줄였어도 원본 3D 모델의 파생물이다. 선택지는
셋이다.

1. FAIRINO 의 허락
2. 자체 간이 모델로 교체 — 3D 자세 뷰는 근사 형상으로 충분하다
3. 공식 CAD 배포본의 약관 확인

`Kinematics.cpp` 의 joint origin 수치는 치수 사실이라 성격이 다르고 위험이 낮다.

### SLAMTEC (`aurora_ros`)

두 부분의 상태가 다르다.

| 구성요소 | 상태 |
|---|---|
| `slamware_ros_sdk` (ROS 래퍼) | BSD-2-Clause — 바이너리 재배포 허용, 고지 재수록 조건 |
| `aurora_remote_public` (사전빌드 `.so`) | 저작권 고지만 있고 이용허락 없음 |

래퍼에 딸린 `LICENSE` 의 저작권 연도는 2009–2014 RoboPeak / 2014–2019
Shanghai Slamtec 으로 Aurora 출시 이전이고, 정작 납품할 `.so` 헤더는
2013–2025 SLAMTEC 이다. 그 BSD-2 가 Aurora 바이너리 SDK 를 덮는다고 보기
어렵다.

SLAMTEC 은 RPLIDAR 계열(`rplidar_ros`, `rplidar_sdk`)에는 BSD-2 를 붙였고
Aurora 계열 세 저장소(`aurora_ros`, `aurora_remote_sdk_demo`,
`py_aurora_remote`)에는 붙이지 않았다. FAIRINO 와 달리 의도적일 수 있다.

**Aurora 를 안 쓰면 노출이 0 이 된다.** `aurora` 인자는 세 launch 파일 모두
기본값이 `false` 이고, `aurora` 패키지의 유일한 외부 의존이
`<exec_depend>slamware_ros_sdk</exec_depend>` 다. Aurora S 가 최종 센서 구성에
없다면 `aurora` 와 `slamware_ros_sdk` 를 납품 프리셋에서 빼면 끝난다.
**Aurora S 가 계약 센서인지 먼저 확정해야 한다.**

권리가 정리되더라도 `.so` 안에 정적으로 들어 있는 libjpeg-turbo, libpng,
zlib, MRPT(Univ. of Málaga) 의 고지는 납품 문서에 재수록해야 한다.

### 그 밖

과업지시서 0.5 절의 지정 버전은 Ubuntu 22.04 + ROS 2 Humble 이고 현재는
24.04 + Jazzy 다. 발주기관의 서면 승인이 필요하며 `install.sh` 가 기동 시
경고한다.

재배포 권리가 확인된 나머지 — unitree_sdk2·unitree_ros2(BSD-3),
DFKI 3종(BSD-3), kiss_icp(MIT), librealsense(Apache-2.0), Qt(LGPLv3) — 는
각 고지를 `licenses/` 에 넣는 것으로 충족된다.

## 코드에서 할 일

### 1. SDK 노출면 축소

과업지시서 4 장이 성과물을 라이브러리로 요구하는데, 라이브러리를 내보내는
것과 코드를 감추는 것이 여기서 정면으로 부딪힌다. 현재 설정은 노출을
최대화하는 쪽에 있다.

`HMI_SHARED_CORE` 가 켜지면 `CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS` 가 함께 켜진다.
이것은 오브젝트 파일에서 찾은 전역 심볼을 모두 `.def` 로 만들어 내보내므로,
공개 헤더를 7 개로 줄여도 `hmi_core` 의 소스 50 개에 들어 있는 내부 클래스와
메서드 이름이 DLL export table 과 import library 에 그대로 남는다. Linux 도
기본 가시성이 public 이라 같은 노출이 생긴다.

```cmake
set_target_properties(hmi_core PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN TRUE
    WINDOWS_EXPORT_ALL_SYMBOLS OFF)
include(GenerateExportHeader)
generate_export_header(hmi_core BASE_NAME INSPECTION_HMI)
```

그 뒤 공개 API 에만 `INSPECTION_HMI_EXPORT` 를 붙인다. 생성된 export 헤더는
SDK 에 함께 넣어야 예제가 빌드된다.

strip 만으로는 부족하다. 디버그 정보는 줄지만 동적 심볼과 import library 의
노출은 그대로다. 로봇 쪽 실행 파일에서 `strip --strip-all` 뒤에도 맹글링된
이름 307 개가 `.dynstr` 에 남는 것을 확인했다.

### 2. 설정 저장 위치

현재는 `QSettings` 라 Windows 레지스트리·계정별로 저장된다. 관제 PC 를 여러
명이 쓰면 각자 다른 설정을 보게 되고, **초기값을 채워서 납품할 수가 없다.**
실행 파일 옆 `inspection.ini` 한 곳으로 옮긴다. 위 산출물 구조의
`inspection.ini` 칸은 이 작업이 끝나야 성립한다.

### 3. 로봇 납품 프리셋

관제에는 `release` 프리셋이 있는데 로봇에는 대응물이 없다. 다음을 묶는다.

- `--symlink-install` 금지. 개발 빌드의 `install/` 은 `package.xml` 등이
  소스 트리를 가리키는 심링크라 그대로 납품하면 소스 경로가 따라간다.
- `CMAKE_BUILD_TYPE=Release`. 현재는 비어 있어 패키지마다 자기 CMakeLists 가
  정한 플래그로 갈린다. `hmi_bridge`, `mission_manager`, `lidar_slam`,
  `ground_segmentation_ros2`, `nav2_ground_consistency_costmap_plugin`,
  `slamware_ros_sdk` 는 `-O` 플래그 없이 빌드되고 있다.
- 가시성 hidden, strip, 디버그 심볼은 별도 보관
- testbed와 개발 도구 제외

### 4. 라이선스 선언 정리

우리 패키지의 선언이 엇갈린다. Apache-2.0 선언은 고객에게 재배포·수정 권리를
명시적으로 부여하므로 은닉 방침과 어긋난다.

```
Apache-2.0    mission_manager, lidar_slam, robot_bringup, aurora
Proprietary   hmi_bridge, realsense_d455, velodyne_vlp16
```

실제 권리 방침에 맞춰 통일한다.

## 포장

- `find_package` 지원. 현재 `install(TARGETS)` 에 `EXPORT` 키워드가 없어
  `inspection_hmiTargets.cmake` 가 생성되지 않는다. `BUILD_INTERFACE` 와
  `INSTALL_INTERFACE:include` 를 분리해 넣는다.
- `bridge_protocol.md` 는 관제 런타임에서 빼고 SDK 에만 넣는다. 고객용 공개
  규약 문서로 정제한다.
- 출하 기본값과 현장 산출물을 분리한다. 지도와 촬영물은 커미셔닝에서
  생기는 것이지 공장 출하물이 아니다. 자리를 나눠야 재설치 때 덮어쓰지 않는다.

## 임치본

전체 소스만으로는 재현되지 않는다. 임치의 목적은 하자보수 기간에 제3자가 그
소스만으로 같은 산출물을 다시 만드는 것이므로 빌드 환경까지 고정한다.

- 서브모듈 고정 커밋 — `git submodule status` 출력을 함께 넣는다
- ROS 배포판, Qt 버전, **컴파일러 버전**
- 빌드 이미지

컴파일러는 특히 주의한다. 현 개발 노트북에는 gcc 10·11·12·13 이 설치되어
있고 기본 `/usr/bin/gcc` 가 gcc-11 이다. Ubuntu 24.04 의 기본값은 gcc-13
이므로, 깨끗한 24.04 에서 임치본을 빌드하면 다른 컴파일러가 잡혀 산출물이
달라진다. `install.sh` 는 컴파일러를 고정하지 않는다.

## CI

세 산출물을 서로 다른 아티팩트로 분리해 만들어야 한다. 현재 저장소에는 CI 가
없으므로 보완이 아니라 신규 구축이다.

- Windows/MSVC/Qt 러너, 젯슨/ROS 러너
- SDK zip 만으로 예제가 빌드되는지 검사
- export 심볼 목록을 검사 대상으로 등록 — 노출면 축소가 되돌아가지 않게 한다
- 아티팩트 서명, 고객 런타임 검사

## 확인된 사실

### Python 은 감출 것이 없다

Python 중 납품물에 나가는 것은 launch 파일뿐이고 알고리즘이 없다. 나머지는
개발 도구로 납품되지 않는다.

Cython 은 동작은 한다. launch 로직을 `.pyx` 로 컴파일하고 얇은 껍데기
`.launch.py` 가 그 모듈을 import 하게 하면 ROS 2 가 정상적으로
`LaunchDescription` 을 읽는다. 그러나 Python 은 런타임에 이름으로 객체를
찾으므로 **식별자가 전부 남는다.** `strip --strip-all` 뒤에도 변수명, 함수명,
패키지명, 토픽명이 그대로 읽힌다. launch 파일 내용의 대부분이 바로 그
이름들이다.

결정적으로 ROS 2 는 런타임 introspection 으로 같은 정보를 내준다. 젯슨에
셸이 있는 사람은 `ros2 node list`, `ros2 param dump` 로 노드 그래프와 모든
파라미터 값을 얻는다. launch 와 YAML 은 숨기려 하지 말고, 대신 젯슨을 봉인된
어플라이언스로 납품하는 편이 실효가 있다.

### 개발 빌드에서 새는 것

| | 크기 | 심볼 | `.cpp` 파일명 |
|---|---|---:|---:|
| 현재 | 20M | 35,220 | 27 |
| `strip --strip-all` 후 | 12M | 0 | 21 |

`--symlink-install` 로 만든 `install/` 은 소스 트리를 가리킨다. 납품에 쓰지
않는다.
