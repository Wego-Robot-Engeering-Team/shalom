# MuJoCo B2 AprilTag 검출

기존 전방 카메라에 공식 ROS 2 `apriltag_ros` 검출 노드를 별도로 연결한다.
검출 알고리즘은 upstream의 `apriltag_node`를 사용하고, 이 패키지는 실행과
토픽 연결 및 시뮬레이션용 설정을 제공한다.
[ROS Index](https://index.ros.org/p/apriltag_ros/#jazzy),
[upstream 3.4.0](https://github.com/christianrauch/apriltag_ros/tree/3.4.0).

## 입력과 출력

| 항목 | 기본값 |
| --- | --- |
| 검출 노드 | `/b2/front_camera/apriltag_detector` |
| 이미지 입력 | `/b2/front_camera/image_raw` |
| CameraInfo 입력 | `/b2/front_camera/camera_info` |
| 검출 결과 | `/b2/front_camera/tag_detections` (`apriltag_msgs/msg/AprilTagDetectionArray`) |
| 검출 TF | `b2/front_camera_optical_frame` → `b2/dock_tag_0` |
| 태그 | `36h11`, ID `0` |
| 검출 코너 사이 변 길이 | `0.3125` m |
| 이미지 구독 QoS | `sensor_data` / Best Effort |

설정은 [config/apriltag_sim.yaml](config/apriltag_sim.yaml)에 둔다. 검출기는
`image_rect` 입력을 기존 `image_raw`로 remap한다. MuJoCo 카메라는 왜곡이 없는
이상적인 핀홀 카메라이므로 이 입력에서 별도의 rectification 노드가 필요하지
않다. 이미지와 `CameraInfo`의 timestamp는 같아야 하며, 검출기는 `CameraInfo.P`
행렬로 pose를 추정한다.
[입력과 pose 계약](https://github.com/christianrauch/apriltag_ros/blob/3.4.0/README.md#topics).

`size: 0.3125`는 현재 0.32 m 패널과 701 × 704 픽셀 태그 텍스처의 흑백 경계
비율에서 정한 시뮬레이션 값이다. 흰 여백을 포함한 패널 전체 폭과 다르다.
패널 크기나 텍스처를 바꾸면 함께 갱신해야 하며, 실기 태그의 측량값이나 실기
카메라 보정 결과로 사용하지 않는다.
[태그 크기 정의](https://github.com/AprilRobotics/apriltag#pose-estimation).

## 설치와 빌드

표준 Jazzy 바이너리 패키지는 다음 명령으로 설치할 수 있다.

```bash
sudo apt update
sudo apt install ros-jazzy-apriltag-ros
```

관리자 권한 없이 빌드하는 이 워크스페이스의 구성은 공식 소스를
`src/apriltag_dependencies`에 두고 기존 `install`에 설치한다. 사용한 버전과
commit은 다음과 같다.

| 패키지 | 버전 | Commit |
| --- | --- | --- |
| `apriltag` | `v3.4.5` | `94be783968e5091bcc9972c72c84fd63efce2935` |
| `apriltag_msgs` | `2.0.2` | `62a272ac06c7e17da4f2e23de02030c3163e74db` |
| `apriltag_ros` | `3.4.0` | `6071e2fae440c12d80ddbbf0b2d18aeacf64c417` |

소스가 없는 새 워크스페이스에서는 아래처럼 준비한다. 이미 해당 폴더에 소스가
있으면 clone을 생략한다.

```bash
cd /home/wego/shalom_ws
mkdir -p src/apriltag_dependencies
git clone --branch v3.4.5 --depth 1 https://github.com/AprilRobotics/apriltag.git \
  src/apriltag_dependencies/apriltag
git clone --branch 2.0.2 --depth 1 https://github.com/christianrauch/apriltag_msgs.git \
  src/apriltag_dependencies/apriltag_msgs
git clone --branch 3.4.0 --depth 1 https://github.com/christianrauch/apriltag_ros.git \
  src/apriltag_dependencies/apriltag_ros
```

필요한 ROS/C++ 시스템 의존성이 설치된 환경에서 다음 명령으로 빌드한다.
upstream의 카메라 구동 예제는 실행하지 않으므로 기존 MuJoCo 카메라가 입력을
계속 제공한다.

```bash
source /opt/ros/jazzy/setup.bash
colcon build --base-paths src/apriltag_dependencies \
  --packages-select apriltag apriltag_msgs apriltag_ros \
  --cmake-args -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
  -DBUILD_EXAMPLES=OFF -DBUILD_PYTHON_WRAPPER=OFF \
  -DPython3_EXECUTABLE=/usr/bin/python3 -DPYTHON_EXECUTABLE=/usr/bin/python3
source install/setup.bash
```

기존 B2 MuJoCo 환경에 `mujoco`와 `onnxruntime`이 설치되어 있어야 한다.
APT 또는 소스 의존성 준비 후 변경된 카메라와 bringup 패키지를 빌드하고
환경을 읽는다.

```bash
cd /home/wego/shalom_ws
source /opt/ros/jazzy/setup.bash
colcon build --base-paths src/shalom \
  --packages-select b2_mujoco simulation_bringup
source install/setup.bash
source src/shalom/robot/third_party/b2_simulation/mujoco/b2_mujoco/b2_env.sh
```

다른 터미널에서도 같은 ROS 환경과 workspace 환경을 읽고, 실행 중인
시뮬레이터와 같은 `ROS_DOMAIN_ID`를 사용한다.

## 실행 중인 카메라에 검출기만 연결

기존 카메라가 이미 켜져 있다면 다음 명령은 검출 노드만 추가한다.
시뮬레이터나 이미지 뷰어를 추가로 실행하지 않는다.

```bash
ros2 launch simulation_bringup apriltag.launch.py
```

카메라가 꺼져 있다면 먼저 태그 테스트 장면에서 전방 카메라를 켠다.
아래는 MuJoCo GUI와 RViz 없이 카메라·검출 TF를 확인하는 실행 예다.
`viewer:=false`에서도 카메라 렌더링에는 작동하는 OpenGL backend가 필요하다.

```bash
ros2 launch b2_mujoco b2_sim.launch.py front_camera:=true viewer:=false \
  scene_file:=/home/wego/shalom_ws/src/shalom/robot/third_party/b2_simulation/mujoco/b2_mujoco/models/b2_tag_test_scene.xml
```

그다음 별도 터미널에서 `apriltag.launch.py`를 실행한다. 같은 카메라에
검출기를 중복 실행하면 동일한 tag TF를 여러 노드가 발행하므로, 별도 실행과
아래 풀스택 실행 중 하나를 선택한다.

## 풀스택과 함께 실행

`front_camera`와 `apriltag`는 모두 기본값이 `false`다. 풀스택에서는
`apriltag:=true`를 지정하면 필요한 전방 카메라도 자동으로 켜진다. 아래 예제는
태그 테스트 장면에서 두 옵션을 모두 명시한다.

```bash
ros2 launch simulation_bringup bringup.launch.py \
  front_camera:=true apriltag:=true \
  scene_file:=/home/wego/shalom_ws/src/shalom/robot/third_party/b2_simulation/mujoco/b2_mujoco/models/b2_tag_test_scene.xml
```

카메라와 검출기의 시작 설정을 바꾼 경우 해당 노드를 다시 실행해 적용한다.

## 검출 확인

다음 명령으로 출력과 optical frame 기준의 tag pose를 확인한다.

```bash
ros2 topic echo /b2/front_camera/tag_detections --once
ros2 topic hz /b2/front_camera/tag_detections
ros2 run tf2_ros tf2_echo b2/front_camera_optical_frame b2/dock_tag_0
```

태그가 보이는 프레임에서 기대하는 결과는 비어 있지 않은 `detections` 배열,
`family: tag36h11`, `id: 0`이다. 헤더의 frame은
`b2/front_camera_optical_frame`이어야 하며, 검출 결과와 tag TF는 입력 이미지의
timestamp를 사용한다. 검출 결과 토픽이 발행되어도 `detections: []`이면 해당
프레임에서는 태그를 찾지 못한 것이므로 토픽의 존재나 발행 주기만으로 성공을
판단하지 않는다.

영상은 RViz의 `Image` display로 확인한다. `/b2/front_camera/image_raw`를
지정하고 `Topic` 아래 `Reliability Policy`를 `Best Effort`로 설정한다.
검출 위치를 함께 보고 싶으면 별도의 `TF` display에서 `b2/dock_tag_0`를
확인한다. 이 검출기는 원본 이미지에 검출 박스나 좌표축을 그려 넣지 않는다.

`image_view` 패키지가 설치된 환경에서는 아래 명령도 사용할 수 있다.
현재 워크스페이스 환경에는 이 패키지가 설치되어 있지 않으므로 RViz를 사용한다.

```bash
ros2 run image_view image_view --ros-args \
  -r image:=/b2/front_camera/image_raw
```

출력이 없다면 카메라 이미지와 `CameraInfo`의 발행·동일 timestamp,
Best Effort 구독, 태그 시야와 조명, `36h11`/ID 0 설정을 확인한다. 입력 영상은
존재하는데 검출 결과가 계속 비어 있으면 패널이 충분히 크게 보이는지 확인한다.
태그가 사라져도 TF 조회에 이전 값이 남을 수 있으므로, 현재 검출 배열과
timestamp도 함께 확인한다.

## 검증 결과

공식 의존성 3개와 `simulation_bringup` 빌드를 완료했고, pytest 및 colcon
test에서 10개 검증이 통과했다. 기존 시뮬레이터를 재시작하거나 주행 명령을
보내지 않고, 실행 중인 카메라에 별도 검출기를 연결해 다음을 확인했다.

| 확인 항목 | 결과 |
| --- | --- |
| 8초 수신 | 검출 결과 65개, 이미지·CameraInfo·검출·TF의 timestamp가 일치하는 64세트 |
| 태그 | `tag36h11`, ID `0`, hamming `0`, 최소 decision margin `160.6846` |
| 발행 주기 | 시뮬레이션 시간 0.1초(10 Hz), 실제 시간 약 8.04 Hz |
| MuJoCo ground truth 대비 위치 오차 | 중앙값 0.00818 m, 최댓값 0.00819 m |
| 자세 오차 | 중앙값 0.614도 |
| 코너 재투영 오차 | 0.534 px |

시뮬레이션 시간 대비 실제 시간 수신 주기에서 약 0.8의 비율이 관측됐다.
이는 현재 장면과 카메라 설정에서의 검출 확인이며, 실기 보정이나 도킹 성능
검증 결과는 아니다.

## 연결 범위

여기까지의 출력은 태그 ID와 카메라 기준 상대 pose다. HMI `state/apriltag`,
Nav2 도킹의 `detected_dock_pose`, 지도상 dock 좌표, 도킹 동작에는 아직
연결하지 않는다. 태그가 검출되었다는 사실만으로 도킹 완료나 충전 시작을
의미하지 않는다. 실제 도킹 연결에는 태그와 충전소의 상대 위치 정의 및
pose 변환, 검출 유효기간과 상실 처리 등의 별도 구현이 필요하다.
