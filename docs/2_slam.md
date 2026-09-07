# 2. SLAM

3D LiDAR로 2D 점유격자 지도를 만든다. 지면은 지도에 넣지 않고 벽·기둥·장애물만 남긴다.

## 파이프라인

```
PointCloud2 (/b2/points)
  └─ ground_segmentation      셀 단위 평면 피팅으로 지면/장애물 분리
       ├─ ground_points
       └─ obstacle_points
            └─ ground_filter  지역 지면 기준 높이 밴드만 통과
                 └─ pointcloud_to_laserscan → /obstacle_scan
                      └─ slam_toolbox → /map,  map → odom

kiss_icp (같은 PointCloud2) → odom → base_link
```

핵심은 `ground_filter`가 **고정 높이가 아니라 지금 발밑 지면을 기준으로** 자른다는
점이다. "z가 0.2 m 이하면 지면"으로 자르면 경사면에서 무너지지만, 지역 지면 기준
"0.15 ~ 1.5 m"는 언덕 위에서도 성립한다.

## 실행

```bash
source ~/shalom_ws/src/b2_simulation/mujoco/b2_mujoco/b2_env.sh
ros2 launch application b2_navigation.launch.py robot:=sim
```

Nav2까지 같이 뜬다. 지도만 만들려면 `nav2:=false`로 끈다.

```bash
ros2 launch application b2_navigation.launch.py robot:=sim nav2:=false
```

RViz의 **SLAM Map**에 `/map`이 그려진다. 로봇을 움직여야 채워지므로
[4. 수동 조종](4_teleop.md)으로 한 바퀴 돌린다.

## 지도 저장

```bash
ros2 run nav2_map_server map_saver_cli -f ~/shalom_ws/src/shalom/robot/application/maps/$(date +%F)
```

`.pgm`과 `.yaml`이 생긴다. 저장된 지도로 주행할 때는 `slam:=false`로 slam_toolbox를 끈다.

## 튜닝

높이 밴드는 `robot/application/config/ground_filter_b2.yaml`에서 조정한다.

| 파라미터 | 기본 | 뜻 |
|---|---|---|
| `min_height_above_ground` | 0.15 | 이 아래는 지면 잡음으로 버림 |
| `max_height_above_ground` | 1.50 | 이 위는 무시. B2 키(0.7 m)보다 높게 잡아 머리 위 구조물도 반영 |

지면 판정 자체는 `robot/application/config/gseg3d_b2.yaml`이다. **`lidar_to_ground`는
LiDAR 실제 장착 높이와 반드시 맞춰야 한다** (B2는 `-0.74`).

## 파이프라인만 따로 쓰기

`slam_3d_to_2d`는 B2를 모른다. 다른 로봇에는 토픽과 프레임만 바꿔 붙인다.

```bash
ros2 launch slam_3d_to_2d ground_slam.launch.py \
  pointcloud_topic:=/velodyne_points base_frame:=base_link
```
