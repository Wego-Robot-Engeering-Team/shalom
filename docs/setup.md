# 설치와 빌드

## 워크스페이스 구성

```
~/shalom_ws/src/
├── shalom/                 이 저장소
│   ├── robot/
│   │   ├── lidar_slam/     3D LiDAR 인식 → 2D SLAM (로봇 무관)
│   │   ├── application/    B2 주행 조립 + B2 전용 튜닝
│   │   └── bridge/         HMI TCP ↔ ROS 2 브릿지
│   │   └── camera_streamer/ RealSense 역할·H.264/RTSP 뷰파인더
│   ├── hmi/                관제 GUI와 HMI 전용 testbed
│   ├── common/             HMI·로봇 공통 통신 계약
│   └── docs/               운용·통신 문서
├── b2_simulation/          실기 대체 시뮬레이터 + RL 학습
├── b2_driver/              실기 드라이버
│   └── unitree_msgs/       unitree_go / unitree_api 메시지 (벤더링, BSD-3)
└── third_party/            소스로 함께 관리하는 외부 ROS 패키지
    ├── ground_segmentation/
    ├── ground_segmentation_ros2/
    ├── kiss_icp/
    ├── aurora_ros/        SLAMTEC Aurora S 공식 ROS 2 드라이버 + aarch64 SDK
    ├── librealsense/       RealSense SDK 소스와 USB 권한 규칙
    └── nav2_ground_consistency_costmap_plugin/
```

Unitree 메시지는 `b2_driver/unitree_msgs` 안에 들어 있다. 설치 스크립트가
`unitree_ros2`의 고정 커밋에서 필요한 `unitree_go`·`unitree_api`만 자동으로
가져오므로 따로 받을 필요가 없다.

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
설치한다. `--role robot`은 ROS Jazzy, Nav2, SLAM, RealSense 래퍼/Viewer,
GStreamer/RTSP 개발 헤더만 설치하며 HMI와 시뮬레이터는 설치하지 않는다.

설치가 끝나면 새 셸을 열거나 아래를 실행한 뒤 빌드한다.

```bash
source /opt/ros/jazzy/setup.bash
cd ~/shalom_ws
colcon build --symlink-install --packages-skip inspection_hmi
source install/setup.bash
```

확인:

```bash
ros2 pkg list | rg 'application|shalom_bridge|camera_streamer|realsense2_camera'
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

## Jetson별 H.264 인코더

현재 설치 대상인 **Orin Nano에는 H.264 하드웨어 인코더(NVENC)가 없다.**
`nvv4l2h264enc`가 보이지 않는 것은 JetPack 설치 실패가 아니라 보드 제약이다.
Nano에서 RTSP를 송신하려면 CPU 소프트웨어 인코더를 별도로 승인·성능 검증한다.
AGX Orin처럼 하드웨어 인코더가 있는 목표 장비에서만 아래 확인 후 NVENC 파이프라인을
선택한다.

```bash
gst-inspect-1.0 nvv4l2h264enc
```

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
ros2 launch camera_streamer camera_streamer.launch.py \
  role:=arm serial:=213522250834 autostart:=false

# 전체 스택과 함께
ros2 launch application bringup.launch.py robot:=real \
  cameras:=true arm_camera_serial:=213522250834
```

토픽 이름은 `shalom_bridge` 의 `bridge.yaml` 이 기다리는 것에 맞춰 리맵된다
(`/fr3/camera_2d/image_raw`, `/fr3/camera_3d/points`, `/b2/camera/image_raw`).
이름이 어긋나면 관제 화면의 "센서 상태" 에 카메라가 계속 "신호 없음" 으로
남는데, 나머지는 정상으로 보이므로 눈으로는 늦게 발견된다.

## Aurora S (주행 위치추정 후보)

Aurora S는 젯슨의 유선 전용망에서 동작한다. 기본 주소는 `192.168.11.1`이고
SDK 연결 포트는 `1445`다. `aurora_odometry`는 제조사 드라이버를 감싸,
처음에는 기존 KISS-ICP와 충돌하지 않는 원시 출력만 올린다.

```bash
ros2 launch aurora_odometry aurora_s.launch.py
ros2 topic echo --once /aurora/odom
```

출력 TF와 토픽은 `aurora_odom → aurora_link`, `/aurora/odom`이다. Aurora의
장착 위치·자세 외부파라미터를 실측하기 전에는 이를 `odom → base_link` 또는
Nav2의 주 odom으로 직접 연결하지 않는다. 기존 KISS-ICP와 두 publisher가 같은
TF 변을 소유하면 위치추정이 깨진다.

전체 bring-up에서 원시 Aurora 검증을 함께 하려면 다음 인자를 사용한다.

```bash
ros2 launch application bringup.launch.py robot:=real use_sim_time:=false \
  aurora:=true aurora_ip:=192.168.11.1
```

## 영상 스트리밍 (뷰파인더)

로봇팔을 겨눌 때 쓰는 실시간 화면이다. 실제 점검 사진은 정지 상태에서
원본으로 찍어 NAS 로 가므로(과업지시서 2.2.4), 이 경로는 화질이 아니라
지연으로 평가한다.

영상은 기존 TCP 브릿지가 아니라 **RTSP/RTP(UDP)** 로 따로 보낸다. TCP 는
모든 프레임을 늦게라도 배달하는데, 뷰파인더에서는 늦은 프레임이 잃은
프레임보다 나쁘다 — 화면이 밀리면 조작자가 팔을 더 움직이게 된다. 제어·
상태 트래픽과 대역폭이 섞이지 않는 효과도 있다.

```bash
sudo apt install -y libgstrtspserver-1.0-dev gstreamer1.0-rtsp
```

```bash
# NVENC 요소가 확인된 AGX Orin
ros2 launch camera_streamer camera_streamer.launch.py
```

현재 Orin Nano에는 위 명령을 쓰지 않는다. 소프트웨어 H.264 인코더를 납품 경로에
넣을지는 CPU 부하·라이선스·지연 측정을 마친 뒤 별도 결정한다.

`~/enable` 서비스로 켜고 끈다. 자율주행 중에는 꺼 둔다 — 대역폭은 항법의
것이고 어차피 촬영은 정지 상태에서만 한다.

```bash
ros2 service call /fr3/camera/camera_streamer/enable std_srvs/srv/SetBool "{data: true}"
```

### 포트

방화벽 규칙과 통신 차단 검증 보고서(과업지시서 7.1)에 그대로 적는다.

| 용도 | 포트 |
|---|---|
| RTSP 제어 | 8554/TCP |
| 암 카메라 RTP·RTCP | 5004-5005/UDP |
| 본체 카메라 (예약) | 5006-5007/UDP |

RTSP 서버는 `bind_address` 로 지정한 인터페이스에서만 듣는다. 기본값이
`0.0.0.0` 이 아닌 이유가 이것이다.

### 수신 쪽에서 반드시 지킬 것

`rtspsrc` 의 `latency` 기본값이 **2000 ms** 다. 그대로 두면 2 초 밀린
화면을 보게 되고, UDP 를 쓴 이유가 사라진다.

```
rtspsrc latency=50 drop-on-latency=true
  ! rtph264depay ! h264parse ! avdec_h264 ! videoconvert
  ! appsink sync=false max-buffers=1 drop=true
```

디코더는 소프트웨어(`avdec_h264`, LGPL)를 쓴다. 720p 15 fps 에서 충분히
가볍고, 납품 PC 의 GPU·드라이버 상태를 전제하지 않는다.

### x264enc 은 납품 경로가 아니다

`x264enc` 은 `libx264`(**GPL-2+**)를 링크한다. 개발 PC 검증에만 쓰고 납품
파이프라인에는 넣지 말 것. "NVENC 가 없으면 x264 로" 같은 폴백을 넣는
순간 로봇 소프트웨어 전체가 GPL 이 된다. Nano용 대안은 별도 검토가 필요하다.

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
