# 관제 ↔ 로봇 프로토콜 통신 규약 v1

관제와 로봇 브릿지의 TCP 통신 규약이다.

## 전송

- TCP 포트 `9090`, `TCP_NODELAY` 필수. 상태·지도·미션·SDK 요청은 이 연결만
  사용한다.
- TCP 포트 `9091`, `TCP_NODELAY` 필수. `cmd/estop`, `cmd/estop_release`와
  E-Stop 전용 heartbeat만 이 연결을 사용한다. HMI는 등록한 일반 포트의 다음
  번호를 E-Stop 포트로 사용한다.
- HMI 목록의 생존 확인도 TCP `9090`에 `INSPECTION-PRESENCE/1\\n`을 보내고 같은
  표식을 돌려받는 짧은 probe다. 제어 연결·이벤트를 만들지 않는다.
- 수동 속도 명령은 이 프로토콜에 넣지 않는다. HMI는 같은 번호의 UDP `9090`으로
  `teleop_bridge`에 보내며, 그 UDP 규약과 300 ms lease는 `teleop_bridge`가 소유한다.
- 리틀 엔디언, 최대 프레임 본문 `32 MiB`
- 프레임: `magic("SHLM") | body_len(uint32) | header_len(uint32) | header(JSON) | payload`
- 부분 수신·복수 프레임 수신을 모두 처리한다. magic 또는 길이가 잘못되면 연결을 끊는다.

## 봉투

```json
{"v":1,"t":"pub","ch":"state/pose","ts":1789123456.789,"p":{}}
```

| 필드 | 설명 |
|---|---|
| `v` | 버전. 현재 `1` |
| `t` | `hb`, `sub`, `unsub`, `pub`, `req`, `res`, `evt` |
| `ch` | 채널 (`hb` 제외) |
| `ts` | Unix epoch 초 |
| `p` | JSON 페이로드 |
| `id` | `req`/`res` 상관 ID |
| `robot` | 로봇 식별자 (아래) |
| `seq` | 스트림 순번(선택) |

버전이 다르면 `E_VERSION`을 응답하고 연결을 종료한다. 채널 추가는 호환되지만,
기존 필드의 의미를 바꾸면 버전을 올린다.

### `robot` — 누구에게, 누구로부터

로봇은 **나가는 모든 프레임에** 자기 식별자를 찍는다. 관제는 처음 들은 값을
고정하고, 같은 연결에서 다른 값이 오면 `E_ROBOT_MISMATCH` 를 남기고 연결을
끊는다.

관제가 보내는 프레임에서는 비어 있어도 된다. 빈 값은 "이 연결에 있는 로봇"
이라는 뜻이고, 관제는 식별자를 듣기 전까지 비워 보낸다. 값이 있는데 로봇의
것과 다르면 로봇이 그 명령을 **실행하지 않고** `E_ROBOT_MISMATCH` 로 거절한다.
거절 응답에는 로봇의 실제 식별자가 찍혀 나가므로, 관제는 자기가 어디에 닿았는지
알게 된다.

이 확인이 필요한 이유는 화면이 거짓말을 하지 않기 위해서다. 주소를 잘못 적어
옆 로봇에 붙어도 나머지 화면은 정상으로 보이고, 그 상태로 비상정지를 누르면
아무도 보고 있지 않은 로봇이 선다.

사람이 읽을 이름(`robot_name`)은 `state/system` 에만 실린다. 모든 프레임에
얹으면 초당 수십 번 같은 문자열을 나르게 된다.

## 상태 채널

| 채널 | 내용 |
|---|---|
| `state/pose` | 위치·방향 |
| `state/battery` | 배터리 |
| `state/system` | CPU·GPU·네트워크 및 촬영·팔 실행기 활성 여부 |
| `state/safety` | E-Stop·운용 모드 |
| `state/nav` | 주행 상태·목표 |
| `state/plan` | 계획 경로 |
| `state/trail` | 주행 궤적 |
| `state/arm` | 관절·끝단 자세 |
| `state/arm_pose_presets` | 로봇에 저장된 사용자 팔 자세 프리셋 |
| `state/base` | 본체 자세·동작 권한 |
| `state/apriltag` | 마커 검출 |
| `state/mission` | 점검 시나리오 상태 |
| `state/waypoints` | 점검 지점 |
| `state/missions` | 선택된 지도에 로봇이 저장한 미션 목록과 revision |
| `state/locations` | home·dock 위치 |
| `state/markers` | 측량된 AprilTag 자리 |
| `state/maps` | 로봇이 보유한 지도 목록 |
| `state/active_map` | 로봇이 현재 사용하는 지도 |
| `state/capture_spool` | 촬영 공유 저장소 상태 (기존 채널명 유지) |
| `state/health` | 센서·링크 상태 |
| `evt/log` | 이벤트·경고 |
| `map/occupancy` | PNG 점유격자 |
| `capture/preview` | 저장된 PNG 미리보기 |

`map/occupancy`는 `width`, `height`, `resolution`, `origin`, `encoding: "png"`을
`p`에 넣고 PNG를 payload로 보낸다. `capture/preview`도 metadata를 `p`에 넣고
이미지를 payload로 보낸다.

`state/mission`은 `state`, `mission_id`, `index`(0부터 시작, 활성 단계가 없으면 -1),
`total`, `reason_code`를 보낸다. 진행 인덱스와 총 단계 수는 로봇의 실행 계획 기준이며
저장된 Waypoint 목록의 순서나 크기와 같다고 가정하지 않는다.

`state/maps`의 각 항목은 `id`, `name`, `created_at`(기록된 경우), `active`,
`default`, `waypoint_count`를
가진다. `id`와 `name`은 지도 폴더 이름이다. HMI는 로봇 파일 시스템을 직접 읽지 않고 이
목록만 표시한다. 지도 전환이 성공하면 브릿지는 `state/active_map`과 선택된 지도 기준의 `map/occupancy`,
`state/waypoints`, `state/locations`, `state/markers`, `state/missions`를 다시 보낸다.
저장 파일별 필드와 미션 단계 유형은 [로봇 운용 데이터 형식](operation_data_format.md)에 정리했다.

`state/waypoints`와 `cmd/waypoints/set`의 각 지점에는 안정적인 `id`, 조작자가 지정한
`name`, 지도 좌표계의 `x`·`y`(m), `theta`(라디안, 도착 시 바라볼 yaw)를 사용한다.
HMI는 방향을 도(°)로 표시·편집하고 전송할 때 라디안으로 변환한다. 이전 지도에
`name` 또는 `theta`가 없는 지점은 다시 저장할 때 각각 ID와 0을 채운다.
`status`는 저장 파일의 필드가 아니라 현재 미션에서 계산해 `state/waypoints`에만
보내는 화면용 상태다. 브리지는 저장 요청에 포함된 `status`를 제거한다.
`state/markers`와 `cmd/markers/set`의 각 AprilTag에는 태그 번호 `id`, 지도 좌표계의
태그 중심 `x`·`y`·`z`(m), `yaw`(라디안)를 사용한다. 태그는 윗변이 위를 향하게
수직 벽에 부착된 것으로 가정하며, `yaw`는 지도 +X에서 태그 **앞면의 바깥쪽 법선**까지
반시계 방향으로 잰 각도다. 따라서 위치 추정용 로봇 방향이나 2D 초기 위치의
`theta`와는 다른 값이다. HMI는 각도를 도(°)로 편집하고 라디안으로 저장한다.
구형 `x`·`y` 전용 마커는 읽되 `z`와 `yaw`를 임의로 0으로 채우지 않는다.
두 값이 없는 마커는 향후 3D 태그 기반 위치 보정에 사용할 수 없다. 현재 브리지는
이 좌표를 저장·전송할 뿐이며, 카메라 검출 결과로 로봇 위치를 자동 보정하지 않는다.
향후 보정에는 카메라-본체 외부 보정 `T_base_camera`와 태그 검출 자세
`T_camera_tag`가 더 필요하다. 같은 태그 좌표계로 맞춘 뒤
`T_map_base = T_map_tag · inverse(T_camera_tag) · inverse(T_base_camera)`로 계산한다.
`state/trail`은 현재 이동 작업의 경로다. 새 개별 주행 목표가 Nav2에 수락되거나
미션이 `RUNNING`으로 전환되면 `reset=true`와 빈 점 목록을 발행한다. 초기 위치
지정 때도 경로를 지우고, 새 AMCL 위치가 TF에 반영된 뒤부터 다시 기록한다.
미션 내부 단계·일시정지·재개·완료에서는 경로를 유지한다.
지도 선택 전에 브리지는 `metadata.json`이 JSON 객체인지 확인하고,
`waypoints.json`·`locations.json`·`markers.json`·`missions.json`의 목록 필드가
배열인지 검사한다. 형식이 올바르지 않으면 현재 지도를 유지하고 전환을 거부한다.

### 2D 초기 위치 추정

HMI의 `초기 위치` 도구는 지도에서 클릭한 점과 드래그 방향을
`cmd/localization/initial_pose` (`x`, `y`, `theta`, map 좌표계)로 보낸다. 브릿지는 저장된
지도가 활성화되어 있고 좌표가 유한한 값인지 확인한 뒤 ROS `PoseWithCovarianceStamped`를
`/initialpose`에 발행한다. 로봇 정지 여부는 확인하지 않으며, 이 명령은 본체를 이동시키지 않고
위치 추정만 갱신한다. 주행 중 위치 추정을 바꾸면 진행 중인 경로 추종에 영향을 줄 수 있으므로
조작자는 지도와 로봇의 실제 위치를 확인해야 한다. 성공 응답은 `/initialpose` 발행을 뜻하며,
AMCL 수렴 여부까지 보장하지는 않는다.

### 로봇 소유 미션 라이브러리

`state/missions` 페이로드는 `{ "map_id": "…", "missions": [...] }`이며 각 미션은
`id`, `name`, `revision`, `archived`, `steps`를 가진다. 미션 정의는 지도별
`missions.json`으로 로봇에 저장된다. 저장/보관 요청은 `expected_revision`을 비교하므로
다른 HMI가 먼저 수정한 경우 오래된 편집본을 덮어쓰지 않고 거절한다. `0`은 새 미션 생성에만 쓴다.

단계는 `navigate` (`location_id`), `capture` (`preset`), `arm_move` (`pose`), `dock` 유형을
사용한다. 현재 실행기는 등록 위치로의 `navigate` 및 마지막 `dock`만 연결되어 있다.
`capture`와 `arm_move` 단계는 라이브러리에 작성·저장할 수 있지만, 실행 요청 시 해당
executor가 연결되어 있지 않으면 미션 전체를 시작 전에 거절한다. 부분 실행은 하지 않는다.

### 로봇 소유 팔 자세 프리셋

`state/arm_pose_presets`는 `{ "presets": [...] }`를 보내며 각 항목은 `id`, `name`,
`description`, `positions`(FR3 6축 라디안), `revision`, `archived`를 가진다. 로봇의
`robot_data_dir/arm_pose_presets.json`에 저장하며 지도와 무관하다. `cmd/arm/pose_presets/list`로
목록을 요청하고 `cmd/arm/pose_presets/save`에 `{ "preset": { … } }`를 보내 추가한다.
수정은 `cmd/arm/pose_presets/update`에 `preset`과 `expected_revision`을 보내며,
로봇이 저장에 성공한 뒤 다시 발행한 목록으로 HMI를 갱신한다. 다른 HMI가 먼저
수정했다면 revision 불일치로 거절한다.
삭제는 `cmd/arm/pose_presets/archive`에 `id`와 `expected_revision`을 보낸다.
로봇은 해당 자세를 참조하는 미션이 어느 지도에든 있으면 거절하고, 그렇지 않으면
`archived`로 표시해 일반 목록에서 숨긴다. 파일에서는 복구 가능하도록 보존한다.
저장은 관절 수와 FR3 관절 한계를 로봇에서 다시 검사한다. HMI에서 프리셋을 불러오면
목표값과 3D 미리보기만 바뀌며, 실제 동작은 별도의 관절 목표 전송으로 요청한다.

### 로봇 지도 목록

`state/maps`는 로봇의 지도 디렉터리를 조회한 목록이며 각 항목의 `id`와 `name`은
폴더 이름이다. `cmd/maps/rename`은 폴더와 `metadata.json`의 이름을 바꾸고,
기본 지도였다면 `default_map.json`도 갱신한다. 팔 자세 프리셋은 지도 밖에 있어 영향받지 않는다.
`cmd/maps/delete`는 사용 중이 아니고 시작 지도도 아닌 지도 폴더 전체를 로봇의
`mapsDir/.trash`로 옮긴다. 폴더 안의 웨이포인트와 미션도 함께 보관되며 일반 목록에서는
빠진다. `cmd/maps/select`가 성공하면 로봇이 새 지도의 웨이포인트·미션을 발행한다.
`cmd/maps/list`는 목록과 현재 지도의 웨이포인트·미션·고정 위치·마커·이미지를
함께 재발행한다. HMI가 빠르게 재접속해도 지도별 자료를 다시 받을 수 있다.
`cmd/maps/set_default`는 `{"id":"지도 폴더명"}`을 받아 로봇의
`maps_dir/default_map.json`을 갱신한다. 빈 ID는 기본 지도 해제다. 현재 실행 중인
지도는 바꾸지 않으며 다음 기동부터 적용된다.

### `state/mission` 의 상태 값

미션 상태는 로봇의 FSM 이 소유한다(`mission_manager`). 관제는 받아서 보여 줄
뿐 스스로 정하지 않는다.

| 값 | 뜻 |
|---|---|
| `idle` | 대기 |
| `running` | 점검 중 |
| `paused` | 일시정지 (조작자 요청·통신 단절·수동 전환) |
| `returning` | 충전 스테이션으로 복귀 중 |
| `completed` | 전체 점검 완료 |
| `fault` | 실패로 멈춤. 명시적 복구가 필요하다 |
| `emergency_stopped` | E-Stop. 해제하면 `paused` 로 내려온다 |

모르는 값을 받으면 `fault` 로 다룬다. `idle` 로 접으면 화면이 "아무 일도 없음"
으로 보이는데, 실제로는 로봇이 무엇을 하는지 모르는 상태다.

## 명령 채널

모든 명령은 `req`/`res`를 쓴다. 수동 속도는 TCP 명령이 아니라 별도 UDP teleop
경로이며, E-Stop은 반드시 전용 TCP `9091` 요청 경로를 사용한다. 일반 `9090`
연결로 온 E-Stop 요청은 거절한다.

| 채널 | 내용 |
|---|---|
| `cmd/estop` | E-Stop 발동 |
| `cmd/estop_release` | E-Stop 수동 해제 |
| `cmd/mode` | `auto` 또는 `manual` |
| `cmd/goto` | 목표 자세 |
| `cmd/localization/initial_pose` | 저장된 지도에서 초기 위치 추정값 설정 (`x`, `y`, `theta`) |
| `cmd/nav_cancel` | 주행 취소 |
| `cmd/waypoints/set` | 점검 지점 전체 설정 |
| `cmd/missions/list` | 선택된 지도의 미션 목록 요청 |
| `cmd/missions/save` | 미션 생성/수정 (`mission`, `expected_revision`) |
| `cmd/missions/archive` | 미션 보관 (`id`, `expected_revision`) |
| `cmd/locations/set` | home·dock 전체 설정 |
| `cmd/markers/set` | 마커 전체 설정 |
| `cmd/maps/list` | 로봇 지도 목록 요청 |
| `cmd/maps/select` | `id`로 로봇의 활성 지도 전환 |
| `cmd/maps/rename` | `id`의 지도 폴더 이름 변경 (`name`: 새 폴더 이름) |
| `cmd/maps/set_default` | 다음 기동에 사용할 기본 지도 지정·해제 (`id` 빈 값은 해제) |
| `cmd/maps/delete` | 비활성 지도 전체를 로봇의 `.trash`로 보관 |
| `cmd/trail/snapshot` | 재접속한 HMI에 해당 로봇의 최근 주행 궤적을 `reset=true`로 재전송 |
| `cmd/power/policy` | 배터리 복귀·출발 기준 |
| `cmd/mission/start` | 점검 시작 |
| `cmd/mission/pause` | 점검 일시정지 |
| `cmd/mission/resume` | 점검 재개 |
| `cmd/mission/stop` | 점검 종료 |
| `cmd/arm/preset` | 암 프리셋 |
| `cmd/arm/joint_goal` | 암 관절 목표. 실행기·안전 게이트·팔 제어 권한이 준비되지 않으면 거절한다. 응답 성공은 목표 접수이지 도달 확인이 아니다. |
| `cmd/arm/ee_goal` | 암 끝단 목표. 현재 브리지는 MoveIt2 실행기가 연결되지 않아 거절한다. |
| `cmd/arm/stop` | 암 정지 |
| `cmd/arm/pose_presets/list` | 로봇 팔 자세 프리셋 목록 요청 |
| `cmd/arm/pose_presets/save` | 사용자 팔 자세 프리셋 추가 |
| `cmd/arm/pose_presets/update` | 팔 자세 이름·설명·관절값 수정 (`expected_revision` 필요) |
| `cmd/arm/pose_presets/archive` | 미션 참조 확인 후 팔 자세를 목록에서 삭제 (`id`, `expected_revision` 필요) |
| `cmd/base/posture` | 본체 자세 전환 (앉기·일어서기) |
| `cmd/capture/trigger` | 촬영 |

### `cmd/base/posture` — 본체 자세

`p.posture`에 아래 값 하나를 넣는다. 관제가 보낸 문자열을 그대로 로봇 서비스
이름으로 쓰지 않으므로, 목록에 없는 값은 `E_BAD_PAYLOAD`로 거절한다.

| 값 | 뜻 |
|---|---|
| `balance_stand` | 균형 서기 (주행 가능한 기본 자세) |
| `stand_up` | 일어서기 |
| `stand_down` | 앉기 |
| `recovery_stand` | 넘어짐 복구 |
| `damp` | 관절 힘 빼기 |

판정은 화면이 아니라 로봇이 한다. 화면이 버튼을 잠그더라도 다른 클라이언트가
같은 명령을 보낼 수 있으므로, 브릿지가 아래를 모두 확인한다.

- E-Stop 중이면 `E_ESTOP`.
- 동작 권한이 `arm_active`·`arm_stopping`이면 `E_BUSY`. 팔이 펴진 채 앉으면
  차체나 바닥에 부딪힌다.
- 이동 중에 `stand_down`·`damp`를 받으면 `E_MODE`. 움직이는 중에 앉거나 힘을
  빼면 넘어진다.
- 오도메트리가 1초 이상 끊겨 정지 상태를 확인할 수 없으면 `stand_down`·`damp`는
  `E_HARDWARE`. 정지를 확인하지 못한 채 앉히지 않는다.
- `damp`는 `p.confirm`이 `true`가 아니면 `E_MODE`. 서 있는 상태에서 누르면
  로봇이 그대로 주저앉으므로 실수로 눌리는 것을 막는다.
- 본체 드라이버가 응답하지 않으면 `E_HARDWARE`.

성공하면 브릿지가 `state/base`를 다시 발행한다. 응답 본문에는 자세가 실려
오지 않으므로 결과는 `state/base`로 읽는다.

`state/base`는 `posture`와 `motion_authority`를 보낸다. `motion_authority`는
로봇의 동작 중재 결과(`none`, `base_active`, `base_stopping`, `arm_active`,
`arm_stopping`)이고, 관제는 받아서 보여 줄 뿐 스스로 정하지 않는다.

실기와 시뮬레이터는 같은 자세 제어 서비스 계약을 제공한다. 시뮬레이터가 아직
표현하지 않는 자세는 시뮬레이터 어댑터가 실패로 응답하며, 브릿지는 성공을
꾸며 내거나 실행 환경에 따라 분기하지 않는다.

## 안전·연결

```text
Nav2        →  /motion/nav/cmd_vel          ─┐
teleop(UDP) →  /motion/teleop/cmd_vel       ─┤
브릿지(수동) →  /motion/manual_hold/cmd_vel  ─┴→ twist_mux
                 → /motion/base/cmd_vel → safety_gate → /cmd_vel → 로봇
```

- E-Stop, 통신 두절, 명령 중재는 로봇의 안전 노드 책임이다.
- 관제와 브릿지는 5 Hz 하트비트를 교환한다.
- 안전 노드는 HMI 하트비트가 **1초** 이상 없으면 정지한다.
- 재연결 후 자율주행은 자동 재개하지 않는다.

### 수동과 자율의 우선순위

`twist_mux`가 **teleop(100) > manual_hold(90) > mission(80) > stair(40) >
dock(30) > nav(20)** 순으로 고르고, 각 입력은 **300 ms** 안에 들어온 것만 유효하다. 같은
토픽에 두 발행자를 두면 우선순위가 발행 순서로 정해지므로 중재를 한곳에 모았다.
고른 결과는 `safety_gate`가 `/safety/state`를 보고 통과·0 출력·차단 중 하나로
처리한 뒤에야 로봇에 닿는다.

| 상태 | 로봇으로 나가는 것 |
|---|---|
| `auto`, 조작 입력 없음 | Nav2 |
| `auto`, 조작 입력 중 | 수동. 멈추면 300 ms 뒤 Nav2 로 돌아간다 |
| `manual`, 조작 입력 없음 | 브릿지가 만드는 제자리 명령 |
| `manual`, 조작 입력 중 | 수동 |
| E-Stop | 없음 — 게이트가 발행 자체를 끊는다 |
| 해제 직후(controlled_stop) | 0. 명시적 재개 전까지 자율은 나가지 않는다 |

수동 모드의 제자리 명령은 관제가 아니라 브릿지가 `manual_hold` 로 만든다.
관제가 0 을 스트림하게 하면 링크가 끊긴 순간 유효 시간이 만료되고, 수동
모드인데도 Nav2 가 로봇을 몰기 시작한다.

E-Stop 과 정지의 차이는 게이트가 만든다. `controlled_stop`·`fault` 는 0 을
계속 내보내 로봇을 세워 두고, `e_stop_latched` 는 아무것도 내보내지 않는다 —
0 도 명령이고, 비상정지는 명령하지 않는 것이 맞다. 로봇은 드라이버의 300 ms
명령 시간초과로 선다. 안전 관리자가 조용해지면 게이트는 차단 쪽으로 닫힌다.

모드 전환은 자율주행을 취소하지 않는다. 수동인 동안 자율 출력이 막힐 뿐이고,
`auto` 로 돌아가면 하던 주행이 이어진다. 취소는 `cmd/nav_cancel` 로만 한다.

## 촬영 데이터·위치·건강 상태

- 미리보기는 `capture/preview`로 관제에 보낸다. 원본은 로봇에서 NAS로 직접 전송한다.
- `cmd/capture/trigger`로 촬영을 요청한다. 로봇은 움직이는 중이면 `E_MODE`로
  거절한다 — 과업지시서 2.2.4가 정지 상태 촬영을 요구하므로, 화면이 버튼을
  잠그는 것과 별개로 규칙 자체는 로봇이 지킨다.
- 저장 파일명은 `차량번호_량번호_포인트ID,YYYYMMDDHHMMSS.png`이고, 같은
  이름의 `.json`에 로봇좌표·촬영거리·Apriltag ID가 들어간다.
- 로봇과 HMI는 같은 NAS 공유 폴더를 각각 마운트한다. 로봇은 마운트가 없으면
  촬영을 거절하고 로컬 폴더에 성공한 것처럼 저장하지 않는다. 현재 구현에는
  오프라인 업로드 대기열이 없다.
- `dock`, `home`, 점검 지점은 로봇이 보관한다.
- `state/health`는 센서별 `expected_hz`, `actual_hz`, `last_seen_ms`, `state`와 링크 지표를 보낸다.

## 오류 코드

| 코드 | 의미 |
|---|---|
| `E_VERSION` | 버전 불일치 |
| `E_UNKNOWN_CHANNEL` | 미지원 채널 |
| `E_BAD_PAYLOAD` | 잘못된 페이로드 |
| `E_ESTOP_ENGAGED` | E-Stop 발동 중 |
| `E_MODE` | 현재 모드에서 불가 |
| `E_BUSY` | 선행 동작 진행 중 |
| `E_UNREACHABLE` | 계획·IK 실패 |
| `E_LIMIT` | 한계 초과 |
| `E_HARDWARE` | 하드웨어 오류 |
