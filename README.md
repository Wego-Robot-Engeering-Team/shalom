# 철도차량 하부점검 시스템

GTX-A 차량 하부를 자율주행 로봇(Unitree B2 + Franka FR3)으로 점검하는
시스템이다. 관제 소프트웨어와 로봇측 소프트웨어를 한 저장소에서 관리한다.

## 구성

**어디서 도는 코드인가**로 나눈다. 기능별로 나누면 같은 기능이 관제와 로봇에
걸칠 때 어디에 둘지가 매번 논쟁이 되고, 무엇을 납품하는지도 흐려진다.

```text
robot/              로봇에서 도는 자율주행·SLAM·브릿지·시나리오
hmi/                관제 PC 에서 도는 C++/Qt6 GUI와 HMI 전용 testbed
common/             HMI와 로봇이 함께 쓰는 통신 프레이밍·데이터 계약
docs/               통신 규격과 로봇 자율주행 문서
```

`robot/application/`은 B2 시뮬레이터와 실기를 하나의 launch 인자로 바꾼다.
`robot/slam_3d_to_2d/`는 PointCloud2와 TF 프레임만 받으므로 로봇에 독립적이다.

## 관제 HMI

시뮬레이터와 대역 지도는 관제 UI가 아니다. 개발·검수 때 로봇 없이 화면을 띄우기
위한 것이고, 납품 빌드에는 들어가면 안 된다 — 들어가면 현장에서 로봇이 안 붙었을 때
조용히 가짜 데이터로 도는 화면이 만들어진다.

`release` 프리셋은 `HMI_WITH_TESTBED=OFF`로 빌드하므로 납품 실행 파일에는
시뮬레이터가 없다. 로봇 주소 없이 켜면 뜨지 않고 그렇게 말한다.

```bash
cmake --preset dev -S hmi
cmake --build --preset dev
./hmi/build/inspection_hmi
```

자세한 내용은 [hmi/README.md](hmi/README.md)를 참조한다. 브릿지는
[robot/bridge/README.md](robot/bridge/README.md), 통신 규격은
[docs/bridge_protocol.md](docs/bridge_protocol.md)에 있다.

## 로봇 자율주행

3D LiDAR 지면분할로 지면을 걸러 2D 지도를 만들고, 그 위에서 Nav2가 주행한다.

```bash
source ~/shalom_ws/src/b2_simulation/mujoco/b2_mujoco/b2_env.sh

ros2 launch application b2_navigation.launch.py robot:=sim
ros2 launch application b2_navigation.launch.py robot:=real network_interface:=enp3s0
```

| 패키지 | 역할 | 아는 것 |
|---|---|---|
| [`robot/slam_3d_to_2d/`](robot/slam_3d_to_2d/) | 3D LiDAR → 지면분할 → 2D 스캔 → 2D SLAM | 로봇을 **모름**. PointCloud2와 TF 프레임만 받는다 |
| [`robot/application/`](robot/application/) | 로봇·인식·Nav2 조립, B2 튜닝, 지도, 조종 | 셋을 아는 유일한 곳 |

```text
robot        b2_simulation (MuJoCo 실기 대체)  또는  b2_driver (실기)
                   │  PointCloud2 ↓        ↑ /cmd_vel
perception   slam_3d_to_2d + kiss_icp
                   │  /map, odom → base_link ↓
planning     Nav2
```

로봇 계층이 launch 인자인 이유는 둘이 같은 인터페이스를 내놓기 때문이다 —
`unitree_go/LowState`가 나가고 `/cmd_vel`이 들어간다.

| 문서 | 내용 |
|---|---|
| [1. 설치와 빌드](docs/1_setup.md) | 워크스페이스 구성, 의존 패키지, 빌드, 시뮬레이터 venv |
| [2. SLAM](docs/2_slam.md) | 파이프라인, 지도 만들기·저장, 튜닝 |
| [3. 내비게이션](docs/3_nav.md) | 목표 보내기, 플래너·컨트롤러 구성 |
| [4. 수동 조종](docs/4_teleop.md) | GUI·스크립트 조종 |

`b2_simulation`과 `b2_driver`는 `~/shalom_ws/src/` 아래의 별도 저장소다.
`ground_segmentation_ros2`와 `kiss_icp`도 워크스페이스에 설치해야 전체 파이프라인이
뜬다. 설치 방법은 [docs/1_setup.md](docs/1_setup.md)에 있다.
