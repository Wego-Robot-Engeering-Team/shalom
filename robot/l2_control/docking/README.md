# Map-owned Dock DB

`docking`은 현재 지도의 `locations.json`을 Nav2 Dock DB로 변환한다.
상주 관리 노드, HMI 프로토콜 변경, 도킹 액션 호출은 포함하지 않는다.
기존 HMI의 “이동” 버튼과 Mission Manager의 충전소 접근 주행은 그대로다.

## 데이터 계약

```text
<map_id>/locations.json (좌표 원본)
  locations[kind == "dock"].x/y/theta
             +
robot/l2_control/navigation/config/nav2.yaml (도킹 플러그인·제어 정책)
             ↓
임시 실행 디렉터리/dock_database.yaml (Nav2용 파생 데이터)
```

- 충전 좌표는 **최종 도킹 목표 위치·방향**이다. `theta` 단위는 rad이다.
- 충전소 앞 접근 위치는 플러그인의 `staging_x_offset` 등의 설정으로 계산한다.
- 지도 폴더에는 좌표와 지도 종속 데이터만 둔다. 생성 YAML을 저장하지 않는다.
- 현재 지도당 충전소 하나이며 Nav2 DB 키는 `dock`이다. `home`은 포함하지 않는다.
- 플러그인 이름은 `nav2.yaml`의 단일 `dock_plugins`에서 읽는다. 별도 하드코딩이나
  시뮬레이션 전용 정책 파일을 추가하지 않는다. 여러 플러그인은 임의 선택하지 않고 거절한다.
- `nav2_docks` 배열은 사용하지 않는다. HMI가 저장하는 기존 `locations`가 원본이다.
- 지도 없음, `locations.json` 없음, 충전 위치 없음은 빈 DB로 처리한다.
  잘못된 JSON, 중복 위치, 누락 좌표, bool/NaN/Inf는 오류로 처리한다.

## 시작 시 적용

`robot_bringup/navigation.launch.py`는 선택한 `map.yaml` 옆의 `locations.json`을
읽고 임시 디렉터리에 DB와 실행용 Nav2 설정을 생성한다. 생성 설정에는
`docking_server.ros__parameters.dock_database` 경로만 주입하며 기존 MPPI,
속도, tolerance, 센서·도킹 플러그인 설정은 유지한다. 실행 종료 시 생성 파일을
정리한다. `nav2:=false`이면 생성하지 않는다.

## 실행 중 갱신

HMI 버튼/브릿지 연결이 보류되어 **지도 선택과 좌표 변경을 자동 감지하지 않는다**.
현재 HMI가 선택한 지도의 경로를 확인한 뒤, 도킹하지 않는 상태에서 아래 명령으로
DB를 다시 적용한다. 이 명령은 좌표 저장이나 지도 선택을 수행하지 않는다.

```bash
source /opt/ros/jazzy/setup.bash
source /home/wego/shalom_ws/install/setup.bash

ros2 run docking reload_dock_database \
  --map /home/wego/shalom_ws/src/shalom/simulation/simulation_bringup/maps/gtxa_19980220_215010/map.yaml \
  --nav2-config /home/wego/shalom_ws/src/shalom/robot/l2_control/navigation/config/nav2.yaml
```

DB를 비우려면 `--map none`을 사용한다. 이 옵션은 실제 지도를 변경하지 않는다.
서버 이름이 다르면 `--service /다른서버/reload_database`를 지정한다.
명령은 선택된 좌표로 DB를 생성하고 `nav2_msgs/srv/ReloadDockDatabase`의
`success` 응답을 확인한 뒤 종료한다. 서비스 대기와 응답 대기를 합한 기본 제한은
5초이며 `--timeout`으로 지정한다. **도킹 액션이나 속도 명령은 전송하지 않는다.**

명령은 Docking Server와 같은 PC/파일 시스템에서 실행해야 한다.
Nav2가 요청의 파일 경로를 직접 읽기 때문이다. 명령 성공/명시적 거절 이후에는
갱신용 임시 파일을 정리한다. 응답 시간 초과는 적용 여부가 불명확하므로 비정상
종료하고 파일을 보존한다. 원격 서비스 요청을 취소했다고 가정하거나 자동 재시도하지 않는다.

Nav2의 `dock_database` 파라미터는 최초 설정 경로이며, reload 서비스가 적용한 DB를
그 파라미터 조회만으로 확인할 수는 없다. 명령의 성공 응답으로 이번 갱신을 확인한다.

## 남은 연결 작업과 안전 경계

- HMI “이동”은 여전히 일반 `NavigateToPose`이다. DB 생성이 이를 도킹으로 바꾸지 않는다.
- HMI 연동 승인 후 선택 지도·위치 변경 완료 시 DB 갱신을 연결해야 한다.
  실패/응답 불명확 시 도킹 요청을 차단하는 상태 관리도 그 연결부에 필요하다.
- DB 갱신 명령 자체는 Safety Gate, 제어권 전환, 충전 확인을 구현하지 않는다.
- 현재 `use_external_detection_pose: false`, `controller.transform_tolerance: 0.5`로
  고정 DB 좌표를 이용한 시뮬레이션 도킹 검증 단계다. AprilTag 검출은 아직
  도킹 pose 입력에 연결하지 않았다. 실기 적용 전 `use_external_detection_pose: true`로
  전환하고 유효기간 내의 최신 `detected_dock_pose` 입력을 연결해야 한다.
  `use_battery_status: false`이므로 도킹 성공은 실제 충전 시작 확인을 의미하지 않는다.
- 현재 반환 주행은 충전 좌표를 Nav2 목표로 사용한다. 이를 도킹 실행으로 전환하는 작업은
  이번 범위에 포함하지 않으며, 최종 도킹 좌표를 무작정 일반 주행 목표로 보내지 않도록 주의한다.

Nav2의 DB 형식·갱신 API는 [Jazzy Docking Framework](https://api.nav2.org/nav2-jazzy/html/md_nav2_docking_README.html)를 따른다.

## 검증

```bash
cd /home/wego/shalom_ws
colcon build --symlink-install --packages-select docking robot_bringup
source install/setup.bash
colcon test --packages-select docking
colcon test-result --verbose
```

테스트는 임시 지도만 사용한다. 실제 Nav2 테스트는 별도 ROS domain에서
Docking Server를 configure하고 DB 최초 로딩·갱신·비우기를 검증한다.
`DockRobot`/`NavigateToPose` goal은 보내지 않으며 실제 충전 검증은 아니다.
