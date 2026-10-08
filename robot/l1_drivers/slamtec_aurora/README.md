# Aurora-S 드라이버

센서 연결·재접속, 카메라·semantic·depth 수신과 픽셀 정렬을 담당한다.
사람 분류·노이즈 제거·2D 투영은 [person_perception](../../l2_perception/person_perception/README.md)에서 처리한다.

## 실행

```bash
source /opt/ros/jazzy/setup.bash
source ~/shalom_ws/install/setup.bash
ros2 launch slamtec_aurora aurora_s.launch.py odom:=false imaging:=true
```

주소 변경: `ip_address:=192.168.11.1`. 설정 파일: `config/aurora_s.yaml`.

| 토픽 | 타입 | 데이터 |
|---|---|---|
| `/aurora/imaging/frame` | `interfaces/msg/SemanticDepthFrame` | 카메라, 원본 semantic, optical XYZ, 깊이 정렬 semantic·texture |

SDK 장치 시각과 ROS 수신 시각을 함께 전달한다. QoS는 best effort, depth 1이다.
Semantic·depth 기능이 활성화된 센서에서 사용한다.

## Odometry

```bash
ros2 launch slamtec_aurora aurora_s.launch.py
```

`config/aurora_s.yaml`에서 주소·발행 주기·프레임을 설정한다.
`/aurora/odom`과 `aurora_odom → aurora_link` TF를 발행하며 기존 내비게이션 TF와 분리한다.

| 옵션 | 기본값 | 실행 |
|---|---|---|
| `odom` | `true` | 제조사 odometry·상태 드라이버 |
| `imaging` | `false` | 카메라·semantic·depth 수신 |

둘 다 사용하려면 `imaging:=true`를 붙인다. 사람 점군 생성과 RViz는 L2에서 실행한다.
