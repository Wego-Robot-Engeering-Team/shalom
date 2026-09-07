# shalom

Unitree B2 자율주행. 3D LiDAR 지면분할로 지면을 걸러 2D 지도를 만들고, 그 위에서
Nav2가 주행한다. **시뮬레이터와 실기를 인자 하나로 바꾼다.**

```bash
source ~/shalom_ws/src/b2_simulation/mujoco/b2_mujoco/b2_env.sh

ros2 launch application b2_navigation.launch.py robot:=sim
ros2 launch application b2_navigation.launch.py robot:=real network_interface:=enp3s0
```

## 패키지

| 패키지 | 역할 | 아는 것 |
|---|---|---|
| [`slam_3d_to_2d/`](slam_3d_to_2d/) | 3D LiDAR → 지면분할 → 2D 스캔 → 2D SLAM | 로봇을 **모름**. PointCloud2와 TF 프레임만 받는다 |
| [`application/`](application/) | 로봇·인식·Nav2 조립, B2 튜닝, 지도, 조종 | 셋을 아는 유일한 곳 |

`slam_3d_to_2d`가 B2를 모르기 때문에 다른 로봇에 그대로 붙는다. 반대로 로봇 계층을
시뮬레이터에서 실기로 바꿔도 인식·계획은 손대지 않는다.

## 계층

```
robot        b2_simulation (MuJoCo 실기 대체)  또는  b2_driver (실기)
                   │  PointCloud2 ↓        ↑ /cmd_vel
perception   slam_3d_to_2d + kiss_icp
                   │  /map, odom → base_link ↓
planning     Nav2
```

로봇 계층이 launch 인자인 이유는 둘이 같은 인터페이스를 내놓기 때문이다 —
`unitree_go/LowState`가 나가고 `/cmd_vel`이 들어간다.

## 문서

| 문서 | 내용 |
|---|---|
| [1. 설치와 빌드](docs/1_setup.md) | 워크스페이스 구성, 의존 패키지, 빌드, 시뮬레이터 venv |
| [2. SLAM](docs/2_slam.md) | 파이프라인, 지도 만들기·저장, 튜닝 |
| [3. 내비게이션](docs/3_nav.md) | 목표 보내기, 플래너·컨트롤러 구성 |
| [4. 수동 조종](docs/4_teleop.md) | GUI·스크립트 조종 |

## 이웃 저장소

`~/shalom_ws/src/` 아래에 나란히 둔다. 별도 Git 저장소이므로 저장소를 넘는 상대
링크는 쓰지 않는다.

| 저장소 | 역할 |
|---|---|
| `b2_simulation` | 실기 대체 시뮬레이터 + RL 정책 학습 |
| `b2_driver` | 실기 드라이버 ([juno-wego/b2_driver](https://github.com/juno-wego/b2_driver)). Unitree 메시지를 `unitree_msgs/`로 벤더링하고 있어 따로 받을 필요가 없다 |


## 현재 상태

`ground_segmentation_ros2`와 `kiss_icp`가 워크스페이스에 없어 파이프라인이 아직
끝까지 뜨지 않는다. 받는 방법은 [docs/1_setup.md](docs/1_setup.md)에 있다.
