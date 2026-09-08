# 철도차량 하부점검 시스템

Unitree B2 사족보행 로봇에 FAIRINO FR3 협동로봇 팔을 얹어, 검수고에 들어온
철도차량 하부를 자율주행으로 돌며 촬영·점검한다.

로봇은 점검포인트 사이를 스스로 이동하고, 벽에 붙은 AprilTag 로 위치를
정밀 보정한 뒤, 팔 끝단의 2D·3D 카메라로 정지 상태에서 촬영한다. 관제
화면은 그 전 과정을 보여주고 조작한다.

## 구성

```text
robot/
  application/     주행 스택 조립 + B2 전용 튜닝 (런치·Nav2·SLAM 설정)
  slam_3d_to_2d/   3D LiDAR 인식 → 2D SLAM (로봇 무관)
  bridge/          관제 TCP ↔ ROS 2 브릿지
  video_streamer/  카메라 영상 H.264 → RTSP (뷰파인더)
hmi/               관제 GUI. ROS 를 쓰지 않는 Qt 프로그램이다.
common/protocol/   관제·로봇 공통 통신 계약
docs/              설치·운용·통신 문서
```

세 갈래가 분명히 나뉜다 — **시뮬레이터**, **실기**, **관제**. 관제 입장에서
시뮬레이터와 실기는 같아야 한다. 다른 것은 접속 주소뿐이다.

## 설치

```bash
cd ~/shalom_ws/src/shalom
./scripts/install.sh --role dev      # robot | station | dev
```

자세한 내용과 이유는 [설치 문서](docs/setup.md)에 있다.

## 실행

### 한 줄로 전부

```bash
source ~/shalom_ws/install/setup.bash
ros2 launch application b2_navigation.launch.py
```

기본값이 이렇게 잡혀 있다 — **시뮬레이터**, **가장 최근 저장 지도**,
**카메라와 뷰파인더 영상**, 관제 브릿지, MuJoCo 뷰어, RViz.

환경 스크립트를 미리 받을 필요는 없다. 노드가 필요한 venv 를 스스로 얹는다.

카메라가 안 꽂혀 있어도 된다 — 한 번 오류를 내고 나머지는 그대로 돈다.

### 실기

```bash
ros2 launch application b2_navigation.launch.py robot:=real
```

카메라를 달았으면 함께 켠다. 시리얼은 두 대 이상일 때 반드시 지정한다
(`rs-enumerate-devices -s` 로 확인).

```bash
ros2 launch application b2_navigation.launch.py robot:=real \
  cameras:=true arm_camera_serial:=213522250834
```

### 지도

기본은 `map:=latest` — `maps/` 에서 가장 최근 것을 불러온다. 이름만 줘도
되고, 새로 그리려면 `none` 이다.

```bash
ros2 launch application b2_navigation.launch.py map:=2026-09-07   # 이름만
ros2 launch application b2_navigation.launch.py map:=none         # 실시간 SLAM
```

저장된 지도를 쓰면 map_server 와 AMCL 이 뜨고 SLAM 은 물러난다. 둘을 함께
켜면 `map→odom` 을 두 노드가 다투므로 자동으로 막는다.

지도를 저장하려면:

```bash
ros2 run nav2_map_server map_saver_cli -f robot/application/maps/$(date +%F)
```

### 관제 화면

로봇 스택과 따로 빌드하고 따로 띄운다.

```bash
cd hmi
cmake --preset dev && cmake --build --preset dev
./build/inspection_hmi            # 브릿지에 접속 (시뮬·실기 모두 여기로 온다)
./build/inspection_hmi --testbed  # 내장 모형 — 로봇도 브릿지도 없을 때
```

### 뷰파인더 영상

팔을 겨눌 때 보는 실시간 화면이다. 실제 점검 사진은 정지 상태에서 원본으로
찍어 NAS 로 가므로, 이 경로는 화질이 아니라 지연으로 평가한다. 제어 채널과
대역폭이 섞이지 않도록 RTSP/RTP 로 따로 보낸다.

메인 런치가 `cameras:=true`(기본값)로 함께 띄운다. 따로 켤 일은 카메라만
시험할 때뿐이다.

```bash
ros2 launch video_streamer video_streamer.launch.py                    # 젯슨
ros2 launch video_streamer video_streamer.launch.py encoder:=x264enc   # 개발 PC
```

주행 중에 대역폭을 아끼려면 끈다.

```bash
ros2 service call /fr3/camera/video_streamer/enable std_srvs/srv/SetBool "{data: false}"
```

### 끄기

```bash
ros2 run application stop_stack
```

자기 프로세스 그룹만 정리한다. 스택 밖의 것까지 쓸어야 할 때만 `--all`.

## 자주 쓰는 인자

| 인자 | 기본값 | 뜻 |
|---|---|---|
| `robot` | `sim` | `sim` 또는 `real` |
| `map` | `latest` | `latest` / 이름 / 경로 / `none`(실시간 SLAM) |
| `cameras` | `true` | 로봇암 RealSense + 뷰파인더 영상 |
| `video` | `true` | RTSP 송신을 바로 켤지 |
| `encoder` | `x264enc` | 젯슨은 `nvv4l2h264enc` |
| `viewer` | `true` | MuJoCo 뷰어 (`robot:=sim` 일 때) |
| `rviz` | `true` | RViz |
| `bridge` | `true` | 관제 브릿지 |
| `payload` | `none` | `fr3` 로 두면 팔을 얹은 시뮬 장면과 정책을 쓴다 |

## 알아 둘 것

**빌드가 `catkin_pkg` 를 못 찾는다고 하면** CMake 가 시스템이 아닌 Python 을
잡은 것이다.

```bash
PATH="/usr/bin:/bin:$PATH" colcon build --symlink-install \
  --cmake-args -DPython3_EXECUTABLE=/usr/bin/python3.12
```

**과업지시서 0.5 절의 지정 버전은 Ubuntu 22.04 + ROS 2 Humble 이다.** 현재
개발은 24.04 + Jazzy 에서 하고 있으며, 이 변경에는 발주기관의 서면 승인이
필요하다. `install.sh` 가 기동 시 이 사실을 알린다.

## 문서

[설치](docs/setup.md) ·
[SLAM](docs/slam.md) ·
[내비게이션](docs/navigation.md) ·
[수동 조종](docs/teleop.md) ·
[통신 규약](docs/bridge_protocol.md)
