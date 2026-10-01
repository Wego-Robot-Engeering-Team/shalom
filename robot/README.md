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
  map:=/var/lib/shalom/maps/inspection_a/map.yaml
```

`map`을 생략하면 `/var/lib/shalom/maps/default_map.json`의 지도를 사용한다. 이 파일이
없거나 `map_id`가 빈 문자열이면 SLAM으로 시작한다. `map:=none`을 명시해도 SLAM으로
시작한다. 지도를 직접 지정할 때는 절대 경로의 `map.yaml`을 사용한다.
지도 번들은 로봇의 `/var/lib/shalom/maps/`에 두며 브리지가 쓸 수 있어야 한다.

VN-100은 기본적으로 실행하지 않는다. 센서를 연결한 뒤 `vn100:=true`와
`vn100_port:=<시리얼 장치>`를 지정하면 raw 가속도·각속도가
`/vn100/imu/data_ned`로 발행된다. 기존 B2 IMU와 내비게이션 입력은 변경하지
않는다. 장치 설정과 좌표계 주의사항은
[VN-100 래퍼](sensors/vectornav_vn100/README.md)를 참고한다.

촬영 기능을 사용할 때는 로봇과 HMI에 동일한 NAS 공유 폴더를 `/mnt/nas`로
마운트한다. 브리지는 `/mnt/nas/inspection`에 PNG와 JSON을 직접 저장하며,
NAS 마운트가 없으면 촬영 요청을 거절한다. 경로를 바꾼다면
`hmi_bridge/config/bridge.yaml`의 `capture.spool_dir`·`capture.mount_point`와
HMI 설정의 촬영 데이터 경로를 함께 바꾼다. 오프라인 업로드 대기열은 없다.

```text
/var/lib/shalom/maps/
├── default_map.json
└── inspection_a/
    ├── map.yaml
    ├── map.pgm
    ├── metadata.json
    ├── waypoints.json
    ├── locations.json
    └── markers.json
```

폴더 이름이 HMI에 표시되는 지도 이름이다. HMI에서 기본 지도를 지정하면
`default_map.json`이 로봇에서 갱신되고 다음 기동부터 적용된다. 실시간 SLAM 중
저장 지도를 선택하면 SLAM을 비활성화하고 map_server·AMCL을 활성화한다.
전환 실패 시 SLAM 복귀를 시도하며, 복귀도 실패하면 수동 복구 오류를 표시한다.
미션 중에는 지도 전환이 거절된다. 명령행에서 `map` 경로를 직접 지정했다면
HMI에서 해당 지도 이름을 바꾼 후 다음 실행 때 경로도 수정해야 한다.

| 인자 | 기본값 | 의미 |
|---|---:|---|
| `lidar` | `xt32` | `xt32` 또는 `none` |
| `aurora` | `false` | Aurora S 드라이버 실행 여부 |
| `aurora_ip` | `192.168.11.1` | Aurora S 주소 |
| `vn100` | `false` | VN-100 드라이버 실행 여부 |
| `vn100_port` | `/dev/ttyUSB_VN100` | VN-100 시리얼 장치 |
| `map` | `auto` | 기본 지도, 절대 경로의 `map.yaml`, 또는 `none`(SLAM) |
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
  --packages-select shalom_interfaces vectornav_driver vectornav_vn100 \
  robot_bringup simulation_bringup hmi_bridge
source install/setup.bash
```
