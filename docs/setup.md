# 1. 설치와 빌드

## 워크스페이스 구성

```
~/shalom_ws/src/
├── shalom/                 이 저장소
│   ├── robot/
│   │   ├── slam_3d_to_2d/  3D LiDAR 인식 → 2D SLAM (로봇 무관)
│   │   ├── application/    B2 주행 조립 + B2 전용 튜닝
│   │   └── bridge/         HMI TCP ↔ ROS 2 브릿지
│   ├── hmi/                관제 GUI와 HMI 전용 testbed
│   ├── common/             HMI·로봇 공통 통신 계약
│   └── docs/               운용·통신 문서
├── b2_simulation/          실기 대체 시뮬레이터 + RL 학습
└── b2_driver/              실기 드라이버
    └── unitree_msgs/       unitree_go / unitree_api 메시지 (벤더링, BSD-3)
```

Unitree 메시지는 `b2_driver` 안에 들어 있다. 따로 받을 필요가 없다.

## ROS와 도구 설치

[ROS 2 Jazzy 설치 문서](https://docs.ros.org/en/jazzy/Installation/Ubuntu-Install-Debs.html)에
따라 apt 저장소를 설정한 뒤:

```bash
sudo apt update
sudo apt install -y \
  ros-jazzy-desktop \
  ros-jazzy-navigation2 ros-jazzy-nav2-bringup \
  ros-jazzy-slam-toolbox ros-jazzy-pointcloud-to-laserscan \
  ros-jazzy-rosidl-generator-dds-idl ros-jazzy-rmw-cyclonedds-cpp \
  ros-dev-tools python3-vcstool python3-dev

sudo rosdep init && rosdep update    # 시스템당 한 번
```

## 카메라 (RealSense)

과업지시서 하드웨어 구성이 카메라 셋을 요구한다 — 본체 하나, 로봇암 끝단에
2D·3D 하나씩. D455 는 컬러와 깊이를 함께 내므로 암 끝단에서는 한 대가 둘을
겸한다.

ROS 2 래퍼는 apt 에 있다. librealsense SDK 를 따로 빌드해 두었더라도 래퍼는
있어야 토픽이 나온다.

```bash
sudo apt install -y ros-jazzy-realsense2-camera ros-jazzy-realsense2-description
```

붙인 카메라의 시리얼을 먼저 확인한다. 두 대 이상 달면 시리얼 없이는 어느
쪽이 열릴지 실행할 때마다 달라진다.

```bash
rs-enumerate-devices -s      # 또는 ros2 run realsense2_camera ...
```

켜기:

```bash
# 카메라만
ros2 launch application cameras.launch.py role:=arm serial:=213522250834

# 전체 스택과 함께
ros2 launch application b2_navigation.launch.py robot:=real \
  cameras:=true arm_camera_serial:=213522250834
```

토픽 이름은 `shalom_bridge` 의 `bridge.yaml` 이 기다리는 것에 맞춰 리맵된다
(`/fr3/camera_2d/image_raw`, `/fr3/camera_3d/points`, `/b2/camera/image_raw`).
이름이 어긋나면 관제 화면의 "센서 상태" 에 카메라가 계속 "신호 없음" 으로
남는데, 나머지는 정상으로 보이므로 눈으로는 늦게 발견된다.

## 제3자 ROS 패키지

apt 에 없어 소스로 받아 둔 것들이다. `third_party/` 에 들어 있으므로 클론 후
따로 받을 필요는 없다.

| 패키지 | 역할 | 라이선스 |
|---|---|---|
| `ground_segmentation` | GSeg3D 지면/장애물 분리 **알고리즘 라이브러리**. 순수 C++ 이고 ROS 를 모른다 | BSD-3 |
| `ground_segmentation_ros2` | 위 라이브러리를 감싼 **ROS 2 노드**. 라이브러리 없이는 빌드되지 않는다 | BSD-3 |
| `kiss_icp` | 3D LiDAR odometry (`odom → base_link`) | MIT |
| `nav2_ground_consistency_costmap_plugin` | local costmap 플러그인 | BSD-3 |

지면분할이 두 개인 것은 나뉘어 배포되기 때문이다 — librealsense SDK 와
`realsense2_camera` 래퍼가 갈려 있는 것과 같은 구조다. 둘 다 있어야 한다.

## 빌드

```bash
cd ~/shalom_ws
source /opt/ros/jazzy/setup.bash
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp    # B2 실기·시뮬레이터 모두 Cyclone DDS

colcon build --symlink-install
source install/setup.bash
```

`catkin_pkg`를 찾지 못한다는 오류가 나면 CMake가 다른 Python(예: `~/.local/bin/python3.11`)을
잡은 것이다. 시스템 Python을 지정한다.

```bash
PATH="/usr/bin:/bin:$PATH" colcon build --symlink-install \
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

납품 구성은 `--preset release` 다. 로그인이 필수이고 testbed 가 빠지며,
코어를 공유 라이브러리(`libhmi_core.so`)로 낸다 — 과업지시서 4장이 S/W
성과물을 `.so` 로 요구한다.

실행:

```bash
./build/inspection_hmi          # 내장 testbed (로봇 없이)
./build/inspection_hmi --live   # 브릿지 연결
```

## 시뮬레이터 환경

`b2_simulation`은 `mujoco`와 `onnxruntime`이 필요하다. apt가 관리하는 시스템 Python을
건드리지 않도록 전용 venv에 넣고 PYTHONPATH로만 얹는다.

```bash
python3 -m venv --system-site-packages ~/shalom_ws/.venv-b2sim
~/shalom_ws/.venv-b2sim/bin/pip install mujoco onnxruntime
```

venv 는 노드가 스스로 찾아 얹는다(`b2_mujoco/_venv.py`). `ros2 launch` 만
해도 뜬다는 뜻이고, 예전처럼 환경 스크립트를 먼저 받지 않아 뷰어가 조용히
안 뜨는 일이 없다.

SDK 도구를 직접 쓸 때만 아래를 받으면 된다. ROS, 워크스페이스, venv, DDS를
한 번에 잡는다.

```bash
source ~/shalom_ws/src/b2_simulation/mujoco/b2_mujoco/b2_env.sh
```
 `CYCLONEDDS_URI`가 셸에 남아 있으면
지워 준다 — 그 설정은 DDS 참가자 인덱스를 고정해서 다중 노드 launch를 깨뜨린다.
