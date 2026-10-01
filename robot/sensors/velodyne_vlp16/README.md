# Velodyne VLP-16

## RViz 데이터 확인

```bash
source /opt/ros/jazzy/setup.bash
source ~/shalom_ws/install/setup.bash
ros2 launch velodyne_vlp16 vlp16_rviz.launch.py
```

드라이버가 이미 실행 중이면 `start_sensor:=false`를 붙인다.
기존 `/velodyne_points` 토픽을 확인할 때는 다음과 같이 실행한다.

```bash
ros2 launch velodyne_vlp16 vlp16_rviz.launch.py \
  start_sensor:=false points_topic:=/velodyne_points
```

| 항목 | 기본값 |
| --- | --- |
| 점군 토픽 (`points_topic`) | `/b2/points` (`sensor_msgs/PointCloud2`) |
| 표시 좌표계 (`fixed_frame`) | `velodyne` |
| 점 색상 | 반사 강도 `intensity` |
| 격자 | 1 m |
| 표시 범위 | 최신 프레임, 누적 없음 |

높이별 색상은 `VLP-16 points → Color Transformer: AxisColor → Axis: Z`로
선택한다. `Measure` 도구로 점 사이 거리를 확인할 수 있다.
다른 프레임의 점군은 `fixed_frame:=<프레임>`으로 지정한다.

## 센서 연결

단독 RViz 실행은 센서 원점을 기준으로 표시한다. 기존 `vlp16.launch.py`는
차량 장착 위치에 맞춘 TF와 `/b2/points` 토픽을 제공한다.

현재 런치의 네트워크 안내는 센서 `192.168.1.201`, PC `192.168.1.100/24`를
기준으로 한다. PC 유선 인터페이스와 센서를 같은 대역에 연결한다.
실제 IP는 센서 설정에 맞춘다.

| 파일 | 역할 |
| --- | --- |
| [`launch/vlp16.launch.py`](launch/vlp16.launch.py) | Velodyne 드라이버·점군 변환·장착 TF |
| [`launch/vlp16_rviz.launch.py`](launch/vlp16_rviz.launch.py) | 센서와 RViz 실행, 기존 토픽 연결 |
| [`rviz/vlp16.rviz`](rviz/vlp16.rviz) | 점군 표시 설정 |
