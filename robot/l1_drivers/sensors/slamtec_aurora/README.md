# Aurora-S 사람 점군 테스트

```bash
cd /home/juno/shalom_ws
source /opt/ros/jazzy/setup.bash
colcon build --base-paths src/shalom/robot/l1_drivers/sensors/slamtec_aurora --packages-select slamtec_aurora --symlink-install
source install/setup.bash
ros2 launch slamtec_aurora person_cloud_test.launch.py
```

Aurora-S 기본 주소는 `192.168.11.1`이다. 다른 주소라면 `ip_address:=<주소>`를 붙인다. RViz 없이 토픽만 확인할 때는 `rviz:=false`를 사용한다.

`/aurora/person/points3d`는 `person`으로 분류된 픽셀의 유효 깊이 포인트 전체를, `/aurora/person/points2d`는 같은 포인트를 로컬 XY 평면(`z=0`)에 정사영한 결과를 발행한다. 두 토픽 모두 `sensor_msgs/PointCloud2`이며, 사람이 보이지 않으면 빈 점군을 발행한다.

`/aurora/person/camera_image`는 왼쪽 카메라 원본, `/aurora/person/overlay_image`는 `person` 영역을 녹색으로 표시한 영상이다. 두 영상도 RViz에 표시된다.

3D 점군에는 깊이 영상과 정렬된 카메라 픽셀 밝기를 `rgb` 필드로 넣는다. 현재 연결된 Aurora-S의 카메라 영상은 흑백(`mono8`)이므로 3D 점군은 실제 밝기에 따른 회색조로 보인다. 2D 투영 점군은 빨간색으로 표시한다.

기본 출력은 카메라 기준 전방 깊이와 평면 거리 모두 3m 이내다. 깊이 영상의 사람 마스크를 안쪽으로 2픽셀 줄이고, 5×5 주변보다 0.25m 넘게 뒤로 튀는 깊이점을 거른다. `mask_erosion_pixels`는 0–3 범위, `max_local_depth_jump_m`은 0 이상에서 조정하며 0은 각 필터 해제다. 거리는 로봇 본체가 아닌 Aurora 카메라 기준이다.

좌표계 `aurora_depth_local`은 카메라 기준 `X` 전방·`Y` 좌측·`Z` 상방이다. 현재 2D 투영은 카메라 장착 자세나 IMU로 중력 정렬한 결과가 아니므로 Nav2 costmap에 직접 연결하지 않는다.

깊이 범위, 라벨 ID, 픽셀 간격, 프레임 시간차 등은 [`config/aurora_s.yaml`](config/aurora_s.yaml)의 `/aurora_person_cloud`에서 조정한다. 현재 장치의 사람 라벨은 `1`이다.
