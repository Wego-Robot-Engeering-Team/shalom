# Aurora-S 사람 점군

L1의 동기화 영상 묶음에서 사람 점군을 추출한다.

## 실행

```bash
source /opt/ros/jazzy/setup.bash
source ~/shalom_ws/install/setup.bash
ros2 launch person_perception person_cloud_test.launch.py
```

Aurora 영상 드라이버와 RViz를 함께 실행한다. `ip_address:=<주소>`, `rviz:=false`로 변경한다.
이미 드라이버가 실행 중이거나 rosbag을 재생할 때는 `driver:=false`를 사용한다.

## 데이터 처리

`/aurora/imaging/frame` → 사람 라벨 선택 → 마스크 침식 → 거리·깊이 노이즈 제거 → 3D 점군·2D 투영.

| 출력 토픽 | 타입 | 내용 |
|---|---|---|
| `/aurora/person/points3d` | `sensor_msgs/msg/PointCloud2` | 사람 XYZ와 카메라 RGB/밝기 |
| `/aurora/person/points2d` | `sensor_msgs/msg/PointCloud2` | 같은 점을 z=0으로 투영, 빨간색 |
| `/aurora/person/camera_image` | `sensor_msgs/msg/Image` | 원본 카메라 |
| `/aurora/person/overlay_image` | `sensor_msgs/msg/Image` | 사람 영역을 초록색으로 표시 |

점군 좌표는 `aurora_depth_local`: X 전방, Y 좌측, Z 위쪽이다.
거리 기준은 Aurora 카메라이며 로봇 장착 TF는 별도로 설정한다.
Nav2·안전 관리자 연동은 별도 구성으로 연결한다.

## 설정

필터: `config/person_cloud.yaml`. 센서 연결·발행 주기: [L1 설정](../../l1_drivers/slamtec_aurora/config/aurora_s.yaml).

| 파라미터 | 현재값 | 의미 |
|---|---:|---|
| `person_label_id` | 1 | 사람 클래스 ID |
| `min_depth_m`, `max_depth_m` | 0.2, 10.0 | 전방 깊이 범위(m) |
| `max_planar_range_m` | 3.0 | 전방·좌우 평면 거리 상한(m) |
| `mask_erosion_pixels` | 2 | 마스크 경계 안쪽 제외 폭, 0–3px |
| `max_local_depth_jump_m` | 0.25 | 5×5 주변보다 뒤로 튀는 깊이 허용치(m), 0은 해제 |
| `pixel_stride` | 1 | 픽셀 샘플링 간격 |
| `min_person_points` | 1 | 최소 유효 점 개수 |
| `sync_tolerance_ms` | 100 | semantic·depth 장치 시각 차이 상한 |
| `input_timeout_ms` | 1000 | 입력 중단 시 점군 초기화 시간 |

범위 밖·비유한 좌표·잘못된 프레임은 제외한다. 입력이 끊기면 빈 점군을 발행한다.
