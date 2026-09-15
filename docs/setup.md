# 설치와 빌드

## 워크스페이스 구성

```
~/shalom_ws/src/
└── shalom/                             최상위 통합 저장소
    ├── robot/
    │   ├── robot_bringup/              platform·navigation·inspection 실행 조립
    │   ├── control/                    미션·안전 관리 (현재 설계 문서)
    │   ├── navigation/config/          B2 주행·위치추정·SLAM 설정
    │   ├── navigation/maps/             저장 지도
    │   ├── navigation/rviz/             주행·지도화 화면 설정
    │   ├── navigation/lidar_slam/       점군 지면분리·2D SLAM
    │   ├── sensors/realsense_d455/      D455 역할·토픽·설정
    │   ├── sensors/slamtec_aurora/      SLAMTEC Aurora S 연동
    │   ├── sensors/pandar_xt32/         Hesai Pandar XT32 연동
    │   ├── sensors/velodyne_vlp16/      임시 VLP-16 연동
    │   ├── hmi_bridge/                 HMI TCP ↔ ROS 2
    │   └── common/                     로봇 전용 공유 코드 배치 기준 (현재 문서)
    ├── hmi/                            관제 GUI와 HMI 전용 testbed
    ├── common/                         HMI·로봇 공통 통신 계약
    ├── docs/                           운용·통신 문서
    └── third_party/                    독립 저장소를 고정한 Git submodule
        ├── b2_driver/                  B2 실기 드라이버
        ├── b2_simulation/              B2 시뮬레이터와 RL 학습
        ├── frcobot_ros2/               FAIRINO FR3 ROS 2 드라이버
        ├── ground_segmentation/
        ├── ground_segmentation_ros2/
        ├── kiss_icp/
        ├── aurora_ros/                 Aurora S ROS 2 드라이버
        ├── hesai_lidar_ros2/            Hesai Pandar ROS 2 드라이버
        ├── librealsense/               RealSense SDK 소스와 USB 권한 규칙
        └── nav2_ground_consistency_costmap_plugin/
```

`third_party`는 회사 밖에서 만든 코드만 뜻하지 않는다. `shalom`과 별도 이력을
가진 저장소 경계라는 뜻이므로 Wego가 관리하는 `b2_driver`와
`b2_simulation`도 여기에 둔다. Unitree 메시지와 SDK는 `b2_driver` 내부의
재귀 서브모듈이며 설치 스크립트가 함께 초기화한다.

ROS 패키지는 폴더와 패키지 이름을 같게 하고, 분야 분류 폴더에는 `package.xml`을 두지
않는다. 기존 작업공간을 갱신한 경우 [구조 변경 후 빌드](../robot/README.md#경로-변경-후-빌드)의
CMake 캐시 갱신 절차를 먼저 따른다. 워크스페이스 안에는 소스 백업을 두지 않는다.

## 설치

절차는 [install.md](install.md)에 있다. 이 문서는 그 절차가 왜 그렇게
생겼는지와, 절차에 담기 애매한 배경만 남긴다.

## 실시간 영상은 두지 않는다

관제에 뷰파인더를 두지 않기로 했다. 과업지시서가 요구하는 것은 정지 상태에서
찍은 촬영 결과(2.2.4)이지 실시간 화면이 아니고, 문서가 실시간 스트림을
언급하는 유일한 자리는 AI 분석 PC 쪽 경로인데 그것은 이번 범위가 아니다.

한동안 RTSP/H.264 송신기를 붙여 두었다가 걷어냈다. 그 경로는 로봇에
GStreamer RTSP 서버와 H.264 인코더를, 관제에 GStreamer 디코더를 요구했다.
Orin Nano에는 NVENC가 없어 소프트웨어 인코더로 내려가는데, `x264enc`은
`libx264`(**GPL-2+**)를 링크하므로 그대로 두면 납품 대상 소프트웨어 전체가
GPL 조건에 걸린다. 실측에서도 소프트웨어 인코딩이 도는 동안 라이다 주기가
규격 10 Hz에서 2~4 Hz로 무너졌다. 요구사항에 없는 기능이 라이선스와 실시간
성능을 동시에 압박한 셈이라 없앴다.

카메라 프레임은 `hmi_bridge`가 직접 구독해 두었다가, 촬영 요청이 오면 그
시점의 원본을 저장하고 미리보기만 관제로 보낸다.

## 제3자 ROS 패키지

apt에 없어 소스로 받아 둔 것들이다. `shalom/third_party/`의 서브모듈로 고정되어
있으므로 `--recurse-submodules`로 클론하면 따로 받을 필요가 없다.

| 패키지 | 역할 | 라이선스 |
|---|---|---|
| `ground_segmentation` | GSeg3D 지면/장애물 분리 **알고리즘 라이브러리**. 순수 C++ 이고 ROS 를 모른다 | BSD-3 |
| `ground_segmentation_ros2` | 위 라이브러리를 감싼 **ROS 2 노드**. 라이브러리 없이는 빌드되지 않는다 | BSD-3 |
| `kiss_icp` | 3D LiDAR odometry (`odom → base_link`) | MIT |
| `nav2_ground_consistency_costmap_plugin` | local costmap 플러그인 | BSD-3 |
| `hesai_ros_driver` | Pandar 계열 패킷을 ROS 2 점군으로 변환하는 공식 Hesai 드라이버 | BSD |

지면분할이 두 개인 것은 나뉘어 배포되기 때문이다 — librealsense SDK 와
`realsense2_camera` 래퍼가 갈려 있는 것과 같은 구조다. 둘 다 있어야 한다.

## 빌드

```bash
cd ~/shalom_ws
source /opt/ros/jazzy/setup.bash
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp    # B2 실기·시뮬레이터 모두 Cyclone DDS

colcon build --base-paths src/shalom --symlink-install
source install/setup.bash
```

`catkin_pkg`를 찾지 못한다는 오류가 나면 CMake가 다른 Python(예: `~/.local/bin/python3.11`)을
잡은 것이다. 시스템 Python을 지정한다.

```bash
PATH="/usr/bin:/bin:$PATH" colcon build --base-paths src/shalom --symlink-install \
  --cmake-args -DPython3_EXECUTABLE=/usr/bin/python3.12
```

## 관제 HMI

로봇 스택과 별개로 빌드한다. ROS 를 쓰지 않는 Qt 프로그램이라 colcon 이
아니라 CMake 로 짓는다.

```bash
sudo apt install -y qt6-base-dev qt6-base-dev-tools qt6-svg-dev

cd ~/shalom_ws/src/shalom/hmi
cmake --preset dev && cmake --build --preset dev
ctest --test-dir build            # 개발 구성
```

실행하면 로그인 창이 먼저 뜬다. 지금은 자리표시 자격증명(`admin` / `admin`)이고,
입력한 이름이 조작 이력에 남는다.

납품 구성은 `--preset release` 다. 내장 모형(testbed)이 빠지며,
코어를 공유 라이브러리(`libhmi_core.so`)로 낸다 — 과업지시서 4장이 S/W
성과물을 `.so` 로 요구한다.

실행:

```bash
./build/inspection_hmi          # 내장 testbed (로봇 없이)
./build/inspection_hmi         # 로봇에 접속 (기본)
./build/inspection_hmi --sim   # 내장 모형 — 로봇도 브릿지도 없을 때
```

## 시뮬레이터 환경

`b2_simulation`은 `mujoco`와 `onnxruntime`이 필요하다. apt가 관리하는 시스템 Python을
건드리지 않도록 전용 venv에 넣고 PYTHONPATH로만 얹는다.

```bash
python3 -m venv --system-site-packages ~/shalom_ws/.venv-b2sim
~/shalom_ws/.venv-b2sim/bin/pip install mujoco onnxruntime
```

venv는 노드가 스스로 찾아 얹는다(`b2_mujoco/_venv.py`). `ros2 launch`만
해도 된다는 뜻이다. 시뮬레이터를 실행하기 전에 별도 환경 스크립트를 받을 필요는 없다.

SDK 도구를 직접 쓸 때만 아래를 받으면 된다. ROS, 워크스페이스, venv, DDS를
한 번에 잡는다.

```bash
source ~/shalom_ws/src/shalom/third_party/b2_simulation/mujoco/b2_mujoco/b2_env.sh
```
`CYCLONEDDS_URI`가 셸에 남아 있으면
지워 준다 — 그 설정은 DDS 참가자 인덱스를 고정해서 다중 노드 launch를 깨뜨린다.
