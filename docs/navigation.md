# 내비게이션

Nav2가 경로를 계획하고 속도 명령을 낸다. B2에서는 보행 정책이 이를 관절 명령으로
바꾼다. 시뮬레이터와 실기는 같은 ROS 인터페이스를 사용한다.

## 실행

새 터미널에서는 먼저 ROS와 워크스페이스를 불러온다.

```bash
source /opt/ros/jazzy/setup.bash
source ~/shalom_ws/install/setup.bash

ros2 launch application b2_navigation.launch.py robot:=sim
ros2 launch application b2_navigation.launch.py robot:=real \
  network_interface:=enp3s0 use_sim_time:=false
```

`robot` 인자만 바꾸면 인식·계획·관제 브릿지는 같은 인터페이스를 사용한다. 실기에서는
반드시 `use_sim_time:=false`를 지정한다.

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

플래너는 `nav2_b2.yaml`에 명시한 **NavFn**(Dijkstra 격자)이다. `allow_unknown: true`라
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

`nav2_b2.yaml`에서 collision monitor의 pointcloud 입력은 **꺼져 있다**. 지면분할이
가까운 지면 반사를 장애물로 잘못 분류하면 로봇 발밑에서 계속 정지가 걸리기 때문이다.
costmap은 그대로 쓰이므로 경로 계획에는 영향이 없다.

**실기에 올릴 때는** 장애물 포인트 스트림을 검증한 뒤
`collision_monitor.pointcloud.enabled`를 `True`로 되돌린다.
