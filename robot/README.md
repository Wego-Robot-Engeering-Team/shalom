# Robot

```text
robot/
├── robot_bringup/             # platform·navigation·inspection launch, DDS 설정
├── navigation/
│   ├── config/                 # Nav2·AMCL·SLAM·KISS-ICP·지면분리 설정
│   ├── maps/                   # 저장 지도
│   ├── rviz/                   # RViz 설정
│   └── lidar_slam/             # 점군 → 2D SLAM
├── tools/                       # 운영·개발 보조 스크립트
├── sensors/
│   ├── realsense_d455/         # Intel RealSense D455 촬영 전용 연동
│   ├── slamtec_aurora/         # SLAMTEC Aurora S 연동
│   ├── pandar_xt32/            # Hesai Pandar XT32 표준 인터페이스 어댑터
│   └── velodyne_vlp16/         # 임시 VLP-16 연동
├── hmi_bridge/                 # HMI TCP ↔ ROS 2
└── control/                    # mission·safety·motion authority control plane
```

## Launch 계층

```text
robot.launch.py        B2·센서·TF 등 플랫폼 인터페이스만
navigation.launch.py   robot + 지면분리·위치추정·SLAM/AMCL·Nav2
inspection.launch.py   navigation + HMI 브리지 + 선택적 RViz
```

`mission_manager`는 현재 HMI bridge가 링크하는 C++ 미션 코어다. supervisory control
노드는 [`control.launch.py`](robot_bringup/launch/control.launch.py)에서 별도로 올린다.
이는 실제 driver topic에 기본 연결되지 않는다. FR3 실기 드라이버도 아직 없으며
`payload:=fr3`은 시뮬레이터 전용이다.

`control/`의 패키지별 책임과 실기 command topic 연결 전제는
[control README](control/README.md)에 있다.

## 실행

새 터미널에서 먼저 실행한다.

```bash
source /opt/ros/jazzy/setup.bash
source ~/shalom_ws/install/setup.bash
```

| 용도 | 명령 |
|---|---|
| B2·센서만 | `ros2 launch robot_bringup robot.launch.py robot:=real use_sim_time:=false network_interface:=<B2-NIC>` |
| 시뮬레이터 내비게이션 | `ros2 launch robot_bringup navigation.launch.py robot:=sim map:=none` |
| 실기 내비게이션 (SLAM) | `ros2 launch robot_bringup navigation.launch.py robot:=real use_sim_time:=false network_interface:=<B2-NIC> map:=none` |
| 저장 지도 위치추정 | `ros2 launch robot_bringup navigation.launch.py robot:=real use_sim_time:=false network_interface:=<B2-NIC> map:=/absolute/path/to/map.yaml` |
| 전체 점검 스택 | `ros2 launch robot_bringup inspection.launch.py robot:=real use_sim_time:=false network_interface:=<B2-NIC>` |
| Supervisory control 시험 | `ros2 launch robot_bringup control.launch.py` |
| Pandar XT32 실기 | `ros2 launch robot_bringup inspection.launch.py robot:=real use_sim_time:=false lidar:=xt32 xt32_config_file:=/etc/shalom/pandar_xt32.yaml` |
| VLP-16 시험 | `ros2 launch robot_bringup inspection.launch.py robot:=real use_sim_time:=false network_interface:=<B2-NIC> lidar:=vlp16 map:=none` |
| 종료 | `~/shalom_ws/src/shalom/robot/tools/stop_stack.sh` |

`map:=none`은 실시간 SLAM이고, `map:=latest`(기본값)는 `navigation/maps/`의 최신
지도를 사용한다. 저장 지도로 위치추정을 하려면 `map:=/absolute/path/to/map.yaml`처럼
YAML의 절대 경로를 넘긴다. 새 지도 저장:

```bash
ros2 run nav2_map_server map_saver_cli \
  -f ~/shalom_ws/src/shalom/robot/navigation/maps/$(date +%F)
```

## RViz만 보기

이미 실행 중인 로봇·센서·SLAM·Nav2를 관찰할 때는 이 launch만 띄운다. 이 명령은
로봇이나 센서 노드를 새로 실행하지 않는다.

```bash
ros2 launch robot_bringup rviz.launch.py profile:=slam       # 점군·TF·지도·odometry
ros2 launch robot_bringup rviz.launch.py profile:=nav2       # costmap·경로·Nav2 Goal
ros2 launch robot_bringup rviz.launch.py profile:=slam_nav2  # 둘을 합친 기본 화면
```

시뮬레이터를 보고 있으면 `use_sim_time:=true`를 추가한다.

`robot`·`use_sim_time`·`network_interface`·`lidar`·`d455`는 세 launch에 공통이다.
`map`, `slam`, `nav2`는 navigation·inspection에, `bridge`, `rviz`, `rviz_profile`은
inspection에만 적용된다.

## 장치 단독 시험

```bash
# D455 촬영용 프레임 입력. HMI로의 연속 영상 전송은 하지 않는다.
ros2 launch realsense_d455 d455_capture.launch.py serial:=<D455-시리얼>

# Aurora S 원시 /aurora/odom
ros2 launch slamtec_aurora aurora_s.launch.py ip_address:=<AURORA-IP>

# VLP-16 점군과 TF
ros2 launch velodyne_vlp16 vlp16.launch.py \
  x:=<x-m> y:=<y-m> z:=<z-m> yaw:=<yaw-rad>

# Pandar XT32 점군과 TF
ros2 launch pandar_xt32 xt32.launch.py \
  config_file:=/etc/shalom/pandar_xt32.yaml \
  x:=<x-m> y:=<y-m> z:=<z-m> yaw:=<yaw-rad>

# HMI TCP 브릿지
ros2 launch hmi_bridge bridge.launch.py use_sim_time:=false
```

## Jetson 빌드

Jetson에서는 Qt HMI를 빌드하지 않는다.

```bash
cd ~/shalom_ws
source /opt/ros/jazzy/setup.bash
colcon build --base-paths src/shalom --symlink-install \
  --cmake-clean-cache --packages-skip inspection_hmi
source install/setup.bash
```
