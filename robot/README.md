# Robot

실기와 시뮬레이션이 공유하는 로봇 스택이다.

## 구조

```text
robot/
├── common/interfaces/       # 공통 msg·srv·action
├── l1_drivers/              # 센서·장치 연결과 원본 데이터 수신
├── l2_perception/           # 지면분리·사람 점군·SLAM
├── l3_control/              # Nav2·도킹·명령 선택·안전 게이트
├── l4_system/               # 미션·안전·운용 권한 관리
├── l5_gateway/              # HMI TCP·E-Stop TCP·수동 조작 UDP
├── bringup/robot_bringup/    # 전체 실행 조립·공통 설정
├── third_party/             # 외부 드라이버·SDK·알고리즘 서브모듈
└── utils/                   # 운용 보조 기능·점검 도구
```

## 실행

```bash
source /opt/ros/jazzy/setup.bash
source ~/shalom_ws/install-layers/setup.bash

# 실기
ros2 launch robot_bringup bringup.launch.py \
  network_interface:=<B2-NIC>

# 시뮬레이션
ros2 launch simulation_bringup bringup.launch.py
```

다음 인자는 실기 bringup 기준이다.

| 인자 | 기본값 | 의미 |
| --- | --- | --- |
| `map` | `auto` | 기본 지도 사용. `none`은 SLAM, 절대 경로의 `map.yaml`은 지정 지도 사용 |
| `maps_dir` | `/var/lib/shalom/maps` | 지도 저장 경로 |
| `robot_data_dir` | `/var/lib/shalom` | 팔 자세·주행 속도 설정 저장 경로 |
| `lidar` | `xt32` | `xt32` 또는 `none` |
| `aurora` | `false` | Aurora S 실행. 주소는 `aurora_ip`로 지정 |
| `vn100` | `false` | VN-100 실행. 장치는 `vn100_port`로 지정 |
| `rviz` | `false` | 내비게이션 RViz 실행 |

`map:=auto`는 `<maps_dir>/default_map.json`을 읽는다. 기본 지도 설정이 없으면 SLAM으로 시작한다.
시뮬레이션 구성과 예시 지도는 [`simulation_bringup`](../simulation/simulation_bringup/)에서 관리한다.

## 설정과 데이터

| 항목 | 경로 |
| --- | --- |
| 로봇 ID | [robot_metadata.yaml](bringup/robot_bringup/config/robot_metadata.yaml) |
| 내비게이션 설정 | [navigation/config/](l3_control/navigation/config/) |
| HMI·촬영 설정 | [bridge.yaml](l5_gateway/hmi_bridge/config/bridge.yaml) |
| 지도·웨이포인트·마커·미션 | `<maps_dir>/<지도 이름>/` |
| 기본 지도 | `<maps_dir>/default_map.json` |
| 로봇팔 자세 | `<robot_data_dir>/arm_pose_presets.json` |
| 주행 속도 설정 | `<robot_data_dir>/navigation_settings.json` |

실행 계정에 데이터 저장 경로의 쓰기 권한이 필요하다.
촬영 시 로봇과 HMI에 같은 NAS를 `/mnt/nas`로 마운트하며, 기본 촬영 경로는 `/mnt/nas/inspection`이다.

센서 실행·RViz: [XT32](l1_drivers/pandar_xt32/README.md),
[VLP-16](l1_drivers/velodyne_vlp16/README.md),
[VN-100](l1_drivers/vectornav_vn100/README.md),
[Aurora S](l1_drivers/slamtec_aurora/README.md).
사람 점군 테스트는 [person_perception](l2_perception/person_perception/README.md)을 참고한다.

## 경로 변경 후 빌드

폴더 재배치 후에는 기존 `build/`·`install/`과 분리해 빌드한다.

```bash
cd ~/shalom_ws
source /opt/ros/jazzy/setup.bash
MAKEFLAGS=-j2 colcon build --executor parallel --parallel-workers 2 \
  --base-paths src/shalom --symlink-install \
  --build-base build-layers --install-base install-layers \
  --packages-up-to robot_bringup simulation_bringup
source install-layers/setup.bash
```

설치 절차: [install.md](../docs/install.md). 데이터 형식: [operation_data_format.md](../docs/operation_data_format.md).
