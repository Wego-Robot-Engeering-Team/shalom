# Shalom

B2 기반 이동 로봇의 점검 스택이다. HMI는 실기와 시뮬레이터를 구분하지 않고,
TCP 브릿지가 있는 로봇의 IP와 포트에만 연결한다.

```text
shalom/
├── robot/
│   ├── common/                         # 공통 ROS 2 인터페이스
│   ├── l4_communication/               # HMI·SDK 통신과 명령 변환
│   ├── l3_system/                      # Mission·Safety·Motion Authority FSM
│   ├── l2_control/                     # navigation·joint mux·safety gate
│   ├── l1_drivers/sensors/             # 센서 어댑터·설정·시험 RViz
│   ├── third_party/                    # 드라이버·알고리즘·MuJoCo 독립 저장소
│   ├── bringup/robot_bringup/          # 실기 실행 조립과 로봇 메타데이터
│   └── tools/                          # 운영·개발 도구
├── simulation/simulation_bringup/      # 시뮬레이션 실행·시나리오·예시 지도
├── hmi/                                # Qt 관제 프로그램과 고객 SDK
├── common/protocol/                    # HMI·SDK·bridge 공통 TCP framing
├── docs/                               # 개발·운용 문서
└── deploy/                             # 로봇 배포 패키징
```

공통 FSM·BT·미션·navigation·HMI bridge는 `robot/`에 한 번만 둔다. 실기와
시뮬레이터는 그 공통 코드를 공유하고, 달라지는 것은 플랫폼 계층뿐이다.

Mission·Safety·Motion Authority의 목표 상태와 B2 시뮬레이션 검증 범위는
[제어 아키텍처 계약](docs/control_architecture_contract.md)에 정의한다.

## 빌드

```bash
cd ~/shalom_ws
source /opt/ros/jazzy/setup.bash
MAKEFLAGS=-j2 colcon build --executor parallel --parallel-workers 2 --base-paths src/shalom --symlink-install \
  --cmake-clean-cache --packages-up-to robot_bringup simulation_bringup
source install/setup.bash
```

공통 메시지 패키지는 `interfaces`다. 의존 패키지까지 함께 빌드한다.

## 로봇 실행

```bash
ros2 launch robot_bringup bringup.launch.py \
  network_interface:=<B2-NIC> \
  maps_dir:=/var/lib/shalom/maps \
  map:=latest
```

## 시뮬레이터 실행

```bash
ros2 launch simulation_bringup bringup.launch.py
```

## 지도

지도 하나는 로봇이 소유하는 디렉터리 하나다.

```text
<maps_dir>/<map_id>/
├── map.yaml
├── map.pgm
├── metadata.json
├── waypoints.json
├── locations.json
└── markers.json
```

HMI 지도 메뉴의 선택은 단순 화면 전환이 아니다. bridge가 `map_server/load_map`을
호출하고, 선택한 지도 전용 웨이포인트·위치·마커를 다시 전송한다. 주행 또는 미션
중에는 전환할 수 없다.

상세 실행 인자와 지도 경로는 [robot README](robot/README.md)를 따른다.
