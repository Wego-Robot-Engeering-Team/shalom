# Shalom

B2 기반 이동 로봇의 점검 스택이다. HMI는 실기와 시뮬레이터를 구분하지 않고,
TCP 브릿지가 있는 로봇의 IP와 포트에만 연결한다.

```text
shalom/
├── robot/                  # 제품 공통 동작 코드와 실기 플랫폼 bringup
│   ├── control/            # FSM·BT·미션·안전·motion authority
│   ├── interfaces/         # Mission·Safety·Motion Authority ROS 2 계약
│   ├── hmi_bridge/         # HMI TCP ↔ ROS 2, 지도 카탈로그 API
│   ├── navigation/         # Nav2·AMCL·SLAM·KISS-ICP 설정
│   └── robot_bringup/      # B2 실기·XT32·Aurora와 공통 stack 조립
├── simulation/             # Wego 소유 시뮬레이션 조립·시나리오·예시 지도
│   └── simulation_bringup/
├── hmi/                    # Qt 관제 프로그램
├── sdk/                    # 고객 연동 SDK 헤더·문서·샘플
├── common/protocol/        # HMI·SDK·bridge 공통 TCP framing
└── third_party/            # 독립 외부 저장소
    └── b2_simulation/      # MuJoCo B2 자체 구현
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
  --packages-select shalom_interfaces b2_mujoco hmi_bridge robot_bringup simulation_bringup
source install/setup.bash
```

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
