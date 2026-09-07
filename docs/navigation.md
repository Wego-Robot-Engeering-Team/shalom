# 3. 내비게이션

Nav2가 경로를 계획하고 `/cmd_vel`을 낸다. B2에서는 그 `/cmd_vel`을 강화학습 보행
정책이 관절 명령으로 바꾼다 — 시뮬레이터든 실기든 같다.

## 실행

```bash
source ~/shalom_ws/src/b2_simulation/mujoco/b2_mujoco/b2_env.sh

ros2 launch application b2_navigation.launch.py robot:=sim
ros2 launch application b2_navigation.launch.py robot:=real network_interface:=enp3s0
```

`robot` 인자만 바꾸면 된다. 시뮬레이터와 실기가 같은 인터페이스를 내놓기 때문에
인식·계획 쪽은 어느 쪽이 도는지 모른다.

| 인자 | 기본 | 뜻 |
|---|---|---|
| `robot` | `sim` | `sim`(MuJoCo) 또는 `real`(실기) |
| `slam` | `true` | slam_toolbox. 저장된 지도를 쓸 땐 `false` |
| `nav2` | `true` | Nav2 스택 |
| `rviz` | `true` | RViz |
| `viewer` | `true` | MuJoCo 창 (`robot:=sim`일 때) |
| `use_sim_time` | `true` | **`robot:=real`이면 `false`로 바꿀 것** |

## 목표 보내기

1. 지도가 비어 있으므로 먼저 조종해서 한 바퀴 돈다
2. RViz 상단 **`Nav2 Goal`** 선택
3. **이미 관측된 빈 공간**을 클릭

초록 경로가 그려지고 로봇이 걸어간다. 관측되지 않은 곳을 찍으면 플래너가 경로를
못 만든다.

## 구성

플래너는 Nav2 기본값인 **NavFn**(Dijkstra 격자)이다. `nav2_b2.yaml`에
`planner_server` 섹션이 없어서 기본값이 쓰인다. 컨트롤러는 명시돼 있다.

```
FollowPath   RegulatedPurePursuitController
  desired_linear_vel      0.6      B2 정책이 안정적으로 추종하는 속도
  lookahead_dist          2.0
  use_rotate_to_heading   true     4족은 제자리 회전이 자연스러움
  allow_reversing         false    RPP가 위와 동시 사용을 금지
footprint  [[0.55, 0.30], [0.55, -0.30], [-0.55, -0.30], [-0.55, 0.30]]
           B2 외형 1.098 x 0.456 m, 다리 스윙을 감안해 폭을 0.60으로
```

local costmap은 `GroundConsistencyLayer`가 3D 지면/장애물 포인트를 직접 받아
확률로 누적한다. global costmap은 장애물 포인트클라우드로 만든다. **Nav2는 SLAM
지도를 계획에 직접 쓰지 않는다** — slam_toolbox는 `map → odom` 보정과 사람이 볼
지도를 담당한다.

## collision monitor

`nav2_b2.yaml`에서 collision monitor의 pointcloud 입력은 **꺼져 있다**. 지면분할이
가까운 지면 반사를 장애물로 잘못 분류하면 로봇 발밑에서 계속 정지가 걸리기 때문이다.
costmap은 그대로 쓰이므로 경로 계획에는 영향이 없다.

**실기에 올릴 때는** 장애물 포인트 스트림을 검증한 뒤
`collision_monitor.pointcloud.enabled`를 `True`로 되돌린다.
