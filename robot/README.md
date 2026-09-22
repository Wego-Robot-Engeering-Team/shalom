# Robot

`robot/`은 제품의 공통 로봇 동작 코드다. FSM·BT·미션·안전·HMI 브릿지·navigation은
실기와 시뮬레이터가 같은 것을 사용한다. MuJoCo와 시나리오 같은 시뮬레이션 전용
자산만 이 디렉터리 밖에 둔다. VLP-16 시험 자산은 어느 기본 bringup에도 포함하지 않는다.

```text
robot/
├── robot_bringup/       # 실기 B2·XT32·Aurora 실행과 공통 navigation 조립
├── navigation/          # Nav2·AMCL·SLAM·KISS-ICP 설정과 RViz profile
├── hmi_bridge/          # HMI TCP ↔ ROS 2, 지도 카탈로그와 지도별 상태 소유
├── interfaces/          # Mission·Safety·Motion Authority ROS 2 계약
├── control/             # mission·safety·motion authority control plane
├── sensors/             # 센서 어댑터 소스 (납품 여부는 runtime package에서 결정)
└── tools/               # 운영·개발 보조 스크립트
```

시뮬레이터 실행 조립과 예시 지도는 [`../simulation/`](../simulation/)에 있다.
`simulation_bringup`은 이 디렉터리의 navigation·HMI bridge·mission core를 그대로
포함한다. MuJoCo 자체는 `third_party/b2_simulation/`에 남는다.

## 실기 실행

```bash
source /opt/ros/jazzy/setup.bash
source ~/shalom_ws/install/setup.bash

ros2 launch robot_bringup bringup.launch.py \
  network_interface:=<B2-NIC> \
  maps_dir:=/var/lib/shalom/maps \
  map:=/var/lib/shalom/maps/2026-09-07/map.yaml
```

`map`을 생략하거나 비워 두면 저장 지도 없이 실시간 SLAM으로 시작한다. 저장 지도를 사용할 때는 반드시 절대 경로의 `map.yaml`을 지정한다. 지도 번들은 로봇의 `/var/lib/shalom/maps/`에 둔다.

```text
/var/lib/shalom/maps/
└── 2026-09-07/
    ├── map.yaml
    ├── map.pgm
    ├── metadata.json
    ├── waypoints.json
    ├── locations.json
    └── markers.json
```

`map_server`와 `hmi_bridge`는 같은 `maps_dir`를 받는다. 따라서 HMI에서 지도를 바꾸면 실제 map_server 지도와 지도별 웨이포인트·위치·마커가 함께 바뀐다. 미션 중에는 지도 전환이 거절된다.

| 인자 | 기본값 | 의미 |
|---|---:|---|
| `lidar` | `xt32` | `xt32` 또는 `none` |
| `aurora` | `false` | Aurora S 드라이버 실행 여부 |
| `aurora_ip` | `192.168.11.1` | Aurora S 주소 |
| `map` | 빈 값 | 절대 경로의 `map.yaml` 또는 빈 값 |
| `maps_dir` | `/var/lib/shalom/maps` | 로봇 소유 지도 번들 경로 |
| `rviz` | `false` | RViz 실행 여부 |

## 시뮬레이터 실행

```bash
ros2 launch simulation_bringup bringup.launch.py
```

시뮬레이터도 같은 TCP HMI 브릿지를 제공한다. HMI에서는 실기와 시뮬레이터를 구분하지 않고, 접속할 IP와 포트만 다르다. 시뮬레이터 지도는 `simulation_bringup` 패키지가 소유하며 HMI 지도 메뉴에서 전환할 수 있다.

## 빌드

```bash
cd ~/shalom_ws
source /opt/ros/jazzy/setup.bash
colcon build --base-paths src/shalom --symlink-install \
  --packages-select shalom_interfaces robot_bringup simulation_bringup hmi_bridge
source install/setup.bash
```
