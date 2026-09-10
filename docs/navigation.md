# 내비게이션

Nav2가 경로를 계획하고 속도 명령을 낸다. B2에서는 보행 정책이 이를 관절 명령으로
바꾼다. 시뮬레이터와 실기는 같은 ROS 인터페이스를 사용한다.

## 실행

새 터미널에서는 먼저 ROS와 워크스페이스를 불러온다.

```bash
source /opt/ros/jazzy/setup.bash
source ~/shalom_ws/install/setup.bash

ros2 launch bringup bringup.launch.py robot:=sim
ros2 launch bringup bringup.launch.py robot:=real \
  network_interface:=enp3s0 use_sim_time:=false
```

`robot` 인자만 바꾸면 인식·계획·관제 브릿지는 같은 인터페이스를 사용한다. 실기에서는
반드시 `use_sim_time:=false`를 지정한다.

## Nano에서 로봇·노트북 HMI 통합 시험

Nano는 AGX 실기 투입 전의 통합 시험기다. Nano와 개발 노트북에 시뮬레이터가 동시에
떠 있으면 기본 ROS 도메인(0)에서 `/clock`과 TF가 섞인다. Nano 시험 스택은 반드시
전용 도메인으로 격리한다. HMI는 ROS가 아니라 TCP 9090만 쓰므로 이 도메인 값과
무관하다.

Nano에서 먼저 D455를 암 카메라 역할로 올린다. 실제 H.264 송신은 Nano에서 켜지지
않는다. Nano에는 NVENC가 없으며, 이 시험은 ROS 카메라 토픽과 HMI 센서 상태를
확인하는 목적이다.

```bash
export ROS_DOMAIN_ID=41
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
source /opt/ros/jazzy/setup.bash
source ~/shalom_ws/install/setup.bash

ros2 launch realsense_d455 d455_stream.launch.py \
  role:=arm serial:=<D455-serial> autostart:=false
```

다른 Nano 터미널에서 시뮬레이터·SLAM·Nav2·TCP 브릿지를 올린다.

```bash
export ROS_DOMAIN_ID=41
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
source /opt/ros/jazzy/setup.bash
source ~/shalom_ws/install/setup.bash

ros2 launch bringup bringup.launch.py \
  robot:=sim payload:=fr3 map:=none rviz:=false viewer:=false \
  cameras:=false
```

노트북에서는 ROS를 실행하거나 같은 도메인에 넣지 않는다. HMI만 Nano 브릿지에
붙인다.

```bash
cd ~/shalom_ws/src/shalom/hmi
./build/inspection_hmi --host <NANO-IP> --port 9090
```

Nano에서 아래가 확인되면 시험 경로가 완성된 것이다.

```bash
ros2 topic echo --once /b2/points --field header.stamp
ros2 run tf2_ros tf2_echo odom base_link
ros2 topic list | rg '^/(map|fr3/camera_2d/image_raw|fr3/camera_3d/points)$'
ss -tn sport = :9090
```

마지막 명령의 peer가 노트북 IP이면 HMI TCP 연결이 실제로 성립한 것이다.

| 인자 | 기본 | 뜻 |
|---|---|---|
| `robot` | `sim` | `sim`(MuJoCo) 또는 `real`(실기) |
| `slam` | `true` | slam_toolbox. 저장된 지도를 쓸 땐 `false` |
| `nav2` | `true` | Nav2 스택 |
| `rviz` | `true` | RViz |
| `viewer` | `true` | MuJoCo 창 (`robot:=sim`일 때) |
| `use_sim_time` | `true` | 실기에서는 반드시 `false` |

## 목표 보내기

1. 지도가 비어 있으므로 먼저 조종해서 한 바퀴 돈다
2. RViz 상단 **`Nav2 Goal`** 선택
3. **이미 관측된 빈 공간**을 클릭

초록 경로가 그려지고 로봇이 걸어간다. 관측되지 않은 곳을 찍으면 플래너가 경로를
못 만든다.

## 구성

플래너는 `robot/navigation/config/nav2.yaml`에 명시한 **NavFn**(Dijkstra 격자)이다. `allow_unknown: true`라
SLAM 지도의 미탐색 영역을 통과하는 계획도 만들 수 있다. 컨트롤러는 MPPI다.

```
FollowPath   MPPIController
  controller_frequency    10 Hz
  motion_model            Omni
  time_steps / model_dt   20 / 0.1 s  (2.0 s 예측 구간)
  vx_max / vx_min         0.6 / -0.4 m/s
  vy_max / wz_max         0.4 m/s / 0.8 rad/s
footprint  [[0.55, 0.30], [0.55, -0.30], [-0.55, -0.30], [-0.55, 0.30]]
           B2 외형과 다리 스윙을 고려한 안전 외곽
```

MPPI는 후보 궤적을 costmap과 전역 경로에 점수화해 명령을 고른다. 다만 현재
`velocity_smoother`가 횡방향 속도(`vy`)를 0으로 제한하므로 실제 출력은 전진·후진·회전만
사용한다.

local costmap은 `GroundConsistencyLayer`가 3D 지면·장애물 포인트를 받아 확률로
누적한다. global costmap은 SLAM의 `/map`을 `static_layer`로 받고, 실시간 장애물
`obstacle_layer`를 그 위에 합친다. 따라서 Nav2는 SLAM 지도를 전역 계획에 사용한다.

## collision monitor

`robot/navigation/config/nav2.yaml`에서 collision monitor의 pointcloud 입력은 **꺼져 있다**. 지면분할이
가까운 지면 반사를 장애물로 잘못 분류하면 로봇 발밑에서 계속 정지가 걸리기 때문이다.
costmap은 그대로 쓰이므로 경로 계획에는 영향이 없다.

**실기에 올릴 때는** 장애물 포인트 스트림을 검증한 뒤
`collision_monitor.pointcloud.enabled`를 `True`로 되돌린다.
