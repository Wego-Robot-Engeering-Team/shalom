# Robot

`robot/`은 통신·시스템 운용·제어 기능·장치 어댑터를 계층별로 관리한다.
FSM·BT·미션·안전·HMI 브릿지·navigation은 실기와 시뮬레이터가 공유한다.
VLP-16 시험 자산은 어느 기본 bringup에도 포함하지 않는다.

```text
robot/
├── common/
│   └── interfaces/                    # Mission·Safety·Motion Authority 계약
├── l4_communication/
│   ├── gateway_transport/             # TCP framing·전송
│   ├── hmi_bridge/                    # 지도·미션·상태 API
│   ├── teleop_bridge/                 # UDP 수동 조작
│   └── estop_bridge/                  # E-Stop·heartbeat 전용 TCP
├── l3_system/
│   ├── mission_manager/               # Mission FSM·BT
│   ├── safety_manager/                # Safety FSM·동작 허가
│   └── motion_interlock_manager/      # base/arm 운용 권한
├── l2_control/
│   ├── docking/                       # 지도 충전 위치 → Nav2 Dock DB
│   ├── navigation/                    # 주행 실행·설정·Nav2 BT·RViz
│   ├── lidar_slam/                    # 점군 지면분리·필터·2D SLAM
│   ├── joint_mux/                     # 관절 명령 source 선택
│   └── safety_gate/                   # 최종 명령 통과·차단
├── l1_drivers/
│   └── sensors/                       # XT32·VLP-16·VN-100·Aurora 어댑터
├── third_party/                       # 별도 Git 이력을 가진 의존 저장소
├── bringup/
│   └── robot_bringup/                 # 실행 조립·DDS 설정·robot_metadata.yaml
└── tools/                             # 운영·개발 보조 스크립트
```

L3는 미션·안전·운용 권한을 판단하고, L2의 mux와 gate는 실제 명령을 선택·차단한다.
L1은 센서·장치 연결을 담당한다. `common` 인터페이스와 bringup은 계층 간 공유 영역이다.
공통 ROS 패키지는 `interfaces`, 도킹 DB 패키지는 `docking`이다.
토픽·서비스 이름은 유지한다.
로봇 ID 설정은 `bringup/robot_bringup/config/robot_metadata.yaml`에 있다.

시뮬레이터 실행 조립과 예시 지도는 [`../simulation/`](../simulation/)에 있다.
`simulation_bringup`은 이 디렉터리의 navigation·HMI bridge·mission core를 그대로
포함한다. MuJoCo 자체는 `third_party/b2_simulation/`에 남는다.

## 실행 구성

실기·시뮬의 `bringup.launch.py`가 내비게이션·시스템·제어·통신·RViz 런치를 직접 호출한다.
실기는 `drivers.launch.py`, 시뮬레이터는 MuJoCo·시뮬 전용 어댑터를 함께 실행한다.

| launch | 역할 |
|---|---|
| `robot_bringup/bringup.launch.py` | 실기 전체 실행, 로봇 ID 설정 |
| `robot_bringup/drivers.launch.py` | L1: B2·XT32·선택 센서 |
| `robot_bringup/control.launch.py` | L2: twist_mux·joint_mux·safety_gate |
| `robot_bringup/system.launch.py` | L3: Mission·Safety·Motion Authority·정지 상태 감시 |
| `robot_bringup/communication.launch.py` | L4: HMI·E-Stop TCP·수동 조작 UDP |
| `navigation/navigation.launch.py` | 기본 지도 선택·검증, 점군 처리·KISS-ICP·SLAM·AMCL·Nav2 |
| `robot_bringup/rviz.launch.py` | 내비게이션 RViz 화면 |

내비게이션 launch·설정·BT·RViz 파일은 `l2_control/navigation` 패키지가 설치한다.
로봇 ID는 실기 bringup에서 읽어 통신 노드에 전달한다.
`control.launch.py`만 실행하면 L3 관리자와 외부 통신은 실행되지 않는다.

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
지도 번들은 로봇의 `/var/lib/shalom/maps/`에 둔다. 로봇팔 자세는 지도와 별도로
`/var/lib/shalom/arm_pose_presets.json`에 저장한다. 로봇 서비스가 두 경로에
쓸 수 있어야 한다.

VN-100은 기본적으로 실행하지 않는다. 센서를 연결한 뒤 `vn100:=true`와
`vn100_port:=<시리얼 장치>`를 지정하면 raw 가속도·각속도가
`/vn100/imu/data_ned`로 발행된다. 기존 B2 IMU와 내비게이션 입력은 변경하지
않는다. 장치 설정과 좌표계 주의사항은
[VN-100 래퍼](l1_drivers/sensors/vectornav_vn100/README.md)를 참고한다.

촬영 기능을 사용할 때는 로봇과 HMI에 동일한 NAS 공유 폴더를 `/mnt/nas`로
마운트한다. 브리지는 `/mnt/nas/inspection`에 PNG와 JSON을 직접 저장하며,
NAS 마운트가 없으면 촬영 요청을 거절한다. 경로를 바꾼다면
`l4_communication/hmi_bridge/config/bridge.yaml`의 `capture.spool_dir`·`capture.mount_point`와
HMI 설정의 촬영 데이터 경로를 함께 바꾼다. 오프라인 업로드 대기열은 없다.

```text
/var/lib/shalom/
├── arm_pose_presets.json
└── maps/
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
| `vn100_port` | `/dev/serial/by-id/usb-FTDI_USB-RS232-WE_AV0LFM92-if00-port0` | VN-100 시리얼 장치 |
| `map` | `auto` | 기본 지도, 절대 경로의 `map.yaml`, 또는 `none`(SLAM) |
| `maps_dir` | `/var/lib/shalom/maps` | 로봇 소유 지도 번들 경로 |
| `robot_data_dir` | `/var/lib/shalom` | 지도와 무관한 팔 자세 저장 경로 |
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
  --cmake-clean-cache --packages-up-to robot_bringup simulation_bringup
source install/setup.bash
```

## 경로 변경 후 빌드

기존 CMake 캐시와 설치 파일을 재사용하지 않고 별도 build/install 경로로 빌드한다.

```bash
cd ~/shalom_ws
source /opt/ros/jazzy/setup.bash
colcon build --base-paths src/shalom --symlink-install \
  --build-base build-layers --install-base install-layers \
  --packages-up-to robot_bringup simulation_bringup
source install-layers/setup.bash
```
