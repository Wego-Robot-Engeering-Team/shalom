# Robot

```text
robot/
├── bringup/                    # 전체 실행 launch, DDS 설정, 운용 스크립트
├── navigation/
│   ├── config/                 # Nav2·AMCL·SLAM·KISS-ICP·지면분리 설정
│   ├── maps/                   # 저장 지도
│   ├── rviz/                   # RViz 설정
│   └── lidar_slam/             # 점군 → 2D SLAM
├── video_streamer/              # 모든 ROS Image의 H.264/RTSP 송신
├── tools/                       # 운영·개발 보조 스크립트
├── sensors/
│   ├── realsense_d455/         # D455 역할·토픽·설정
│   ├── aurora/                 # Aurora S 연동
│   └── velodyne_vlp16/         # 임시 VLP-16 연동
├── communication/hmi_bridge/   # HMI TCP ↔ ROS 2
└── control/                    # 미션·안전 관리 위치 (아직 구현 전)
```

## 실행

새 터미널에서 먼저 실행한다.

```bash
source /opt/ros/jazzy/setup.bash
source ~/shalom_ws/install/setup.bash
```

| 용도 | 명령 |
|---|---|
| 인자 확인 | `ros2 launch bringup bringup.launch.py --show-args` |
| 시뮬레이터 새 지도 | `ros2 launch bringup bringup.launch.py robot:=sim map:=none` |
| 시뮬레이터 저장 지도 | `ros2 launch bringup bringup.launch.py robot:=sim map:=2026-09-07` |
| 실기 B2 | `ros2 launch bringup bringup.launch.py robot:=real use_sim_time:=false network_interface:=<B2-NIC>` |
| 실기 + VLP-16 | `ros2 launch bringup bringup.launch.py robot:=real use_sim_time:=false network_interface:=<B2-NIC> lidar:=vlp16 map:=none` |
| 실기 + Aurora 검증 | `ros2 launch bringup bringup.launch.py robot:=real use_sim_time:=false aurora:=true aurora_ip:=<AURORA-IP>` |
| RTSP를 HMI에 공개 | `ros2 launch bringup bringup.launch.py robot:=real use_sim_time:=false video_bind_address:=<로봇-HMI망-IP>` |
| 종료 | `~/shalom_ws/src/shalom/robot/tools/stop_stack.sh` |

`map:=none`은 실시간 SLAM이고, `map:=latest`(기본값)는 `navigation/maps/`의 최신
지도를 사용한다. 새 지도 저장:

```bash
ros2 run nav2_map_server map_saver_cli \
  -f ~/shalom_ws/src/shalom/robot/navigation/maps/$(date +%F)
```

## 주요 인자

| 인자 | 기본값 | 설명 |
|---|---:|---|
| `robot` | `sim` | `sim` 또는 `real` |
| `use_sim_time` | `true` | 실기에서는 `false` |
| `map` | `latest` | `latest`, 지도 이름, 절대 YAML 경로, `none` |
| `nav2`, `slam`, `rviz` | `true` | 각 기능 On/Off |
| `lidar` | `none` | 임시 VLP-16 시험만 `vlp16` |
| `cameras`, `video` | `true` | D455 / RTSP 송신 On/Off |
| `arm_camera_serial` | 빈 값 | D455가 둘 이상일 때 USB 시리얼 |
| `encoder` | `nvv4l2h264enc` | AGX NVENC. Nano 시험은 `video:=false` |
| `video_bind_address` | `127.0.0.1` | HMI 영상 시험 시 로봇의 HMI망 IP로 지정 |
| `aurora`, `aurora_ip` | `false`, `192.168.11.1` | Aurora S 원시 odometry와 장치 IP |
| `bridge` | `true` | HMI TCP 브릿지(9090) On/Off |
| `domain_id` | `0` | 여러 로봇 DDS 분리 번호 |
| `network_interface` | 빈 값 | 실기 B2 통신 NIC |
| `viewer`, `payload` | `true`, `none` | 시뮬레이터 창 / FR3 탑재 모델 |

RTSP 기본 주소는 `rtsp://<로봇-HMI망-IP>:8554/arm-rgb`다.

## 장치 단독 시험

```bash
# D455 + RTSP
ros2 launch realsense_d455 d455_stream.launch.py \
  role:=arm serial:=<D455-시리얼> bind_address:=<로봇-HMI망-IP>

# Aurora S 원시 /aurora/odom
ros2 launch aurora aurora_s.launch.py ip_address:=<AURORA-IP>

# VLP-16 점군과 TF
ros2 launch velodyne_vlp16 vlp16.launch.py \
  x:=<x-m> y:=<y-m> z:=<z-m> yaw:=<yaw-rad>

# HMI TCP 브릿지
ros2 launch hmi_bridge bridge.launch.py use_sim_time:=false
```

## Jetson 빌드

Jetson에서는 Qt HMI를 빌드하지 않는다.

```bash
cd ~/shalom_ws
source /opt/ros/jazzy/setup.bash
colcon build --symlink-install --cmake-clean-cache --packages-skip inspection_hmi
source install/setup.bash
```
