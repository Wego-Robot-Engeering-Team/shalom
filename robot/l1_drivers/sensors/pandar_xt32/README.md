# Pandar XT32

## RViz 데이터 확인

```bash
source /opt/ros/jazzy/setup.bash
source ~/shalom_ws/install/setup.bash
ros2 launch pandar_xt32 xt32_rviz.launch.py
```

드라이버가 이미 실행 중이면 `start_sensor:=false`를 붙인다.
센서 IP·포트·보정 설정은 `config/xt32.yaml`에서 지정하며,
다른 설정 파일은 `config_file:=/etc/shalom/pandar_xt32.yaml`로 선택한다.

| 항목 | 기본값 |
| --- | --- |
| 점군 토픽 (`points_topic`) | `/b2/points` |
| 표시 좌표계 (`fixed_frame`) | `pandar_xt32` |
| 점 색상 | 반사 강도 `intensity` |
| 격자 | 1 m |
| 표시 범위 | 최신 프레임, 누적 없음 |

높이로 색을 구분하려면 RViz의 `XT32 points → Color Transformer`를
`AxisColor`, `Axis`를 `Z`로 변경한다. 마우스로 회전·확대하고 `Measure` 도구로
거리를 확인할 수 있다. 단독 실행은 센서 원점을 기준으로 표시한다.

## 로봇 연결

`pandar_xt32` is the project adapter for the official Hesai ROS 2 driver.  It
does not contain a fork of the driver: that source is pinned at
`robot/third_party/hesai_lidar_ros2`.

It publishes the standard interface expected by the rest of the robot:

| Item | Value |
|---|---|
| Point cloud | `/b2/points` (`sensor_msgs/PointCloud2`) |
| Driver frame | `pandar_xt32` |
| Stack LiDAR frame | `b2/lidar_link` |

Before commissioning, create a site-specific copy of `config/xt32.yaml` and
set the Pandar's IP/UDP/PTC ports and calibration policy to the device's web
configuration. Measure the `base_link → pandar_xt32` mount transform; the
launch defaults are only a temporary B2 mounting-position example.

```bash
ros2 launch pandar_xt32 xt32.launch.py \
  config_file:=/etc/shalom/pandar_xt32.yaml \
  x:=<metres> y:=<metres> z:=<metres> yaw:=<radians>
```

Use `lidar:=xt32` in the `robot_bringup` launches to include it in the full
stack. Ubuntu installation and NIC setup are in [docs/install.md](../../../../docs/install.md).
