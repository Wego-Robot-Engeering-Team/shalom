# 설치와 빌드

## 워크스페이스 구성

```
~/shalom_ws/src/
├── shalom/                 이 저장소
│   ├── robot/
│   │   ├── bringup/                    전체 실행·시스템 설정 (ROS 패키지)
│   │   ├── control/                    미션·안전 관리 (현재 설계 문서)
│   │   ├── navigation/config/          B2 주행·위치추정·SLAM 설정
│   │   ├── navigation/maps/             저장 지도
│   │   ├── navigation/rviz/             주행·지도화 화면 설정
│   │   ├── navigation/lidar_slam/       점군 지면분리·2D SLAM
│   │   ├── sensors/realsense_d455/      D455 역할·토픽·설정
│   │   ├── sensors/aurora/              Aurora S 연동
│   │   ├── sensors/velodyne_vlp16/      임시 VLP-16 연동 (최종 XT32)
│   │   ├── communication/hmi_bridge/   HMI TCP ↔ ROS 2
│   │   └── common/                     로봇 전용 공유 코드 배치 기준 (현재 문서)
│   ├── hmi/                관제 GUI와 HMI 전용 testbed
│   ├── common/             HMI·로봇 공통 통신 계약
│   └── docs/               운용·통신 문서
├── b2_simulation/          사내 관리: 실기 대체 시뮬레이터 + RL 학습
├── b2_driver/              사내 관리: Unitree B2 실기 드라이버
│   └── unitree_msgs/       unitree_go / unitree_api 메시지 (벤더링, BSD-3)
└── third_party/            외부 ROS·SDK 소스 (필요한 호환 수정은 설치 스크립트에 기록)
    ├── ground_segmentation/
    ├── ground_segmentation_ros2/
    ├── kiss_icp/
    ├── aurora_ros/         SLAMTEC Aurora S ROS 2 드라이버 (Jazzy 호환 보정 적용)
    ├── librealsense/       RealSense SDK 소스와 USB 권한 규칙
    └── nav2_ground_consistency_costmap_plugin/
```

Unitree 메시지는 `b2_driver/unitree_msgs` 안에 들어 있다. 설치 스크립트가
`unitree_ros2`의 고정 커밋에서 필요한 `unitree_go`·`unitree_api`만 자동으로
가져오므로 따로 받을 필요가 없다.

ROS 패키지는 폴더와 패키지 이름을 같게 하고, 분야 분류 폴더에는 `package.xml`을 두지
않는다. 기존 작업공간을 갱신한 경우 [구조 변경 후 빌드](../robot/README.md#경로-변경-후-빌드)의
CMake 캐시 갱신 절차를 먼저 따른다. 워크스페이스 안에는 소스 백업을 두지 않는다.

## 한 번에 설치

아래 절들의 명령을 모아 둔 스크립트가 있다. 손으로 옮겨 치면 반드시 하나를
빠뜨린다.

```bash
cd ~/shalom_ws/src/shalom
./scripts/install.sh --role dev       # 개발 PC: 전부
./scripts/install.sh --role robot     # 로봇: 주행·카메라·영상 송신
./scripts/install.sh --role station   # 관제 PC: Qt·영상 수신
./scripts/install.sh --role dev --dry-run   # 무엇을 깔지 먼저 보기
```

역할을 나누는 이유는 로봇에 Qt 가, 관제 PC 에 RealSense 드라이버가 필요
없기 때문이다. 안 쓰는 것을 깔아 두면 납품 시 의존성 목록만 길어지고
라이선스 고지 대상도 함께 는다.

아래 절들은 각 명령이 왜 필요한지를 남겨 둔 것이다. 스크립트를 쓰면
따로 실행할 필요는 없다.

## Jetson 로봇에 새로 설치하기

이 절은 Jetson Orin Nano/AGX에 JetPack 7.2.1 (Ubuntu 24.04)을 설치한 직후의
재현 가능한 절차다. **Nano용 SSD를 AGX로 옮겨 부팅하지 않는다.** 각 보드는
자기 보드 대상으로 JetPack을 플래시한 뒤 아래를 각각 수행한다.

SDK Manager의 JetPack 추가 구성요소 설치가 끝난 뒤, Jetson에서 실행한다.
처음에는 유선망 또는 인터넷이 필요하다.

```bash
mkdir -p ~/shalom_ws/src
cd ~/shalom_ws/src

# `main`은 초기 저장소이므로, 실제 로봇 소스가 있는 `dev` 브랜치를 받는다.
git clone --branch dev --single-branch \
  https://github.com/Wego-Robot-Engeering-Team/shalom.git shalom

cd shalom
./scripts/install.sh --role robot
```

`sources.repos`는 실제 로봇 실행에 필요한 B2 드라이버, apt에 없는 ROS 패키지,
RealSense SDK 소스를 검증된 커밋으로 함께 받는다. 설치 스크립트가 ROS의 `vcs`
도구를 먼저 설치한 뒤 자동으로 가져오고, SDK에 포함된 D4xx USB UDEV 규칙도
설치한다. `--role robot`은 ROS Jazzy, Nav2, SLAM, RealSense 래퍼/Viewer만 설치하며
HMI와 시뮬레이터는 설치하지 않는다.

설치가 끝나면 새 셸을 열거나 아래를 실행한 뒤 빌드한다.

```bash
source /opt/ros/jazzy/setup.bash
cd ~/shalom_ws
colcon build --symlink-install --packages-skip inspection_hmi
source install/setup.bash
```

확인:

```bash
ros2 pkg list | rg 'bringup|hmi_bridge|realsense_d455|realsense2_camera'
realsense-viewer
```

`realsense-viewer`에서 D455가 보이면 RGB·Depth 스트림을 켜서 확인한다. UDEV 규칙을
막 설치했거나 권한 경고가 보이면 카메라 USB를 한 번 뺐다가 다시 연결한다.

`src/third_party/librealsense`는 SDK 소스와 UDEV 규칙을 고정하기 위해 보관한다.
실행에는 ROS Jazzy가 제공하는 같은 버전의 SDK·Viewer를 사용하므로, 설치 스크립트가
SDK 소스에는 `COLCON_IGNORE`를 둔다. 따라서 ROS 빌드에서 SDK가 두 번 빌드되거나
서로 다른 `librealsense`가 섞이지 않는다.

고정한 B2 드라이버 커밋에는 더는 쓰지 않는 `dae/` 디렉터리를 CMake 설치 목록에
남긴 부분이 있다. 설치 스크립트가 빈 호환 디렉터리를 자동 생성하므로, 첫 빌드도
별도 수동 수정 없이 진행된다.


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
2D·3D 하나씩. D455는 컬러와 깊이를 함께 내므로 암 끝단에서는 한 대가 둘을
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
ros2 launch realsense_d455 d455_stream.launch.py \
  role:=arm serial:=213522250834 autostart:=false

# 전체 스택과 함께
ros2 launch bringup bringup.launch.py robot:=real \
  cameras:=true arm_camera_serial:=213522250834
```

토픽 이름은 `hmi_bridge` 의 `bridge.yaml` 이 기다리는 것에 맞춰 리맵된다
(`/fr3/camera_2d/image_raw`, `/fr3/camera_3d/points`, `/b2/camera/image_raw`).
이름이 어긋나면 관제 화면의 "센서 상태" 에 카메라가 계속 "신호 없음" 으로
남는데, 나머지는 정상으로 보이므로 눈으로는 늦게 발견된다.

## Aurora S (주행 위치추정 후보)

Aurora S는 젯슨의 유선 전용망에서 동작한다. 기본 주소는 `192.168.11.1`이고
SDK 연결 포트는 `1445`다. `aurora`는 제조사 드라이버를 감싸,
처음에는 기존 KISS-ICP와 충돌하지 않는 원시 출력만 올린다.

```bash
ros2 launch aurora aurora_s.launch.py
ros2 topic echo --once /aurora/odom
```

출력 TF와 토픽은 `aurora_odom → aurora_link`, `/aurora/odom`이다. Aurora의
장착 위치·자세 외부파라미터를 실측하기 전에는 이를 `odom → base_link` 또는
Nav2의 주 odom으로 직접 연결하지 않는다. 기존 KISS-ICP와 두 publisher가 같은
TF 변을 소유하면 위치추정이 깨진다.

전체 bring-up에서 원시 Aurora 검증을 함께 하려면 다음 인자를 사용한다.

```bash
ros2 launch bringup bringup.launch.py robot:=real use_sim_time:=false \
  aurora:=true aurora_ip:=192.168.11.1
```

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

apt에 없어 소스로 받아 둔 것들이다. 워크스페이스 루트의 `third_party/`에 들어 있으므로 클론 후
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

venv는 노드가 스스로 찾아 얹는다(`b2_mujoco/_venv.py`). `ros2 launch`만
해도 된다는 뜻이다. 시뮬레이터를 실행하기 전에 별도 환경 스크립트를 받을 필요는 없다.

SDK 도구를 직접 쓸 때만 아래를 받으면 된다. ROS, 워크스페이스, venv, DDS를
한 번에 잡는다.

```bash
source ~/shalom_ws/src/b2_simulation/mujoco/b2_mujoco/b2_env.sh
```
`CYCLONEDDS_URI`가 셸에 남아 있으면
지워 준다 — 그 설정은 DDS 참가자 인덱스를 고정해서 다중 노드 launch를 깨뜨린다.
