# lidar_slam

Nav2를 3D LiDAR로 확장하는 인식 패키지. **로봇과 시뮬레이터에 무관하다** —
PointCloud2 하나와 TF 프레임만 주면 Gazebo, MuJoCo, 실기에서 똑같이 돈다.

## 왜 필요한가

2D Nav2는 한 평면만 본다. 3D LiDAR를 쓰면 지면까지 장애물로 잡혀 로봇이 아무 데도
못 간다. 그렇다고 "z가 얼마 이하면 지면"이라고 고정 높이로 자르면 경사면에서 무너진다.

이 패키지는 **지면을 셀 단위로 추정한 뒤 그 지면 기준으로** 장애물을 고른다. 언덕의
절대 높이와 무관하게 "지금 발밑 지면에서 0.15~1.2 m"만 남으므로 경사에서도 성립한다.

```
PointCloud2
  └─ ground_segmentation (GSeg3D)   셀 평면 피팅으로 지면/장애물 분리
       ├─ ground_points
       └─ obstacle_points
            └─ ground_filter        지역 지면 기준 높이 밴드만 통과   ← 이 패키지의 노드
                 └─ pointcloud_to_laserscan → /obstacle_scan
                      └─ slam_toolbox → /map, map → odom
```

`odom → base_link`는 **제공하지 않는다.** 휠·다리 오도메트리나 LiDAR 오도메트리
(KISS-ICP)로 따로 공급해야 한다. 한 변에 publisher가 둘이면 TF 트리가 깨진다.

## 실행

```bash
ros2 launch lidar_slam ground_slam.launch.py \
  pointcloud_topic:=/b2/points \
  base_frame:=base_link
```

로봇별 값은 파라미터 파일로 넘긴다. 특히 `gseg3d`의 `robot_frame`과
`lidar_to_ground`(LiDAR 장착 높이)는 반드시 맞춰야 한다.

```bash
ros2 launch lidar_slam ground_slam.launch.py \
  pointcloud_topic:=/b2/points \
  gseg_params_file:=$(ros2 pkg prefix application)/share/shalom/config/gseg3d_b2.yaml \
  ground_filter_params_file:=$(ros2 pkg prefix application)/share/shalom/config/ground_filter_b2.yaml
```

저장된 맵으로 위치추정만 할 때는 `slam:=false`로 slam_toolbox를 끈다.

## 구성

```
src/ground_filter.cpp     지역 지면 기준 밴드 필터 (C++ 노드)
config/
  ground_segmentation.yaml  GSeg3D 기본값 — 로봇별로 덮어쓸 것
  ground_filter.yaml        높이 밴드 기본값
  slam.yaml                 slam_toolbox 기본값
launch/ground_slam.launch.py
```

## 튜닝

`ground_filter`:

| 파라미터 | 뜻 |
|---|---|
| `min_height_above_ground` | 이 아래는 지면 잡음으로 버림 (기본 0.15 m) |
| `max_height_above_ground` | 이 위는 무시 — 나뭇가지, 천장 (기본 1.20 m) |
| `ground_cell_size` | 지면 높이를 추정하는 XY 셀 크기 |
| `ground_search_radius` | 장애물 밑에 지면 반사가 없을 때 이웃 셀을 찾는 반경 |

`ground_segmentation`(GSeg3D):

| 파라미터 | 뜻 |
|---|---|
| `lidar_to_ground` | LiDAR 장착 높이 (음수). **반드시 실측과 맞출 것** |
| `slopeThresholdDegrees` | 이보다 가파르면 지면이 아니라 장애물 |
| `groundInlierThreshold` | 피팅된 지면 평면에서 이 거리까지는 지면. `cellSizeZPhase2`보다 작게 |

## 의존

`ground_segmentation_ros2`(및 그 라이브러리 `ground_segmentation`)가 필요하다.
`src/`에 없으면 launch가 `PackageNotFoundError`로 죽는다.
