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
| `state/system` | CPU·GPU·네트워크 |
| `state/safety` | E-Stop·운용 모드 |
| `state/nav` | 주행 상태·목표 |
| `state/plan` | 계획 경로 |
| `state/trail` | 주행 궤적 |
| `state/arm` | 관절·끝단 자세 |
| `state/base` | 본체 자세·동작 권한 |
| `state/apriltag` | 마커 검출 |
| `state/mission` | 점검 시나리오 상태 |
| `state/waypoints` | 점검 지점 |
| `state/locations` | home·dock 위치 |
| `state/markers` | 측량된 AprilTag 자리 |
| `state/maps` | 로봇이 보유한 지도 목록 |
| `state/active_map` | 로봇이 현재 사용하는 지도 |
| `state/capture_spool` | 촬영 업로드 상태 |
| `state/health` | 센서·링크 상태 |
| `evt/log` | 이벤트·경고 |
| `map/occupancy` | PNG 점유격자 |
| `capture/preview` | JPEG 미리보기 |

`map/occupancy`는 `width`, `height`, `resolution`, `origin`, `encoding: "png"`을
`p`에 넣고 PNG를 payload로 보낸다. `capture/preview`도 metadata를 `p`에 넣고
이미지를 payload로 보낸다.

`state/maps`의 각 항목은 `id`, `name`, `created_at`, `active`, `waypoint_count`를
가진다. `id`는 지도 디렉터리의 변경하지 않는 식별자이고, `name`은 로봇의
`metadata.json`에 저장된 표시 이름이다. HMI는 로봇 파일 시스템을 직접 읽지 않고 이
목록만 표시한다. 지도 전환이 성공하면 브릿지는 `state/active_map`과 선택된 지도 기준의 `map/occupancy`,
`state/waypoints`, `state/locations`, `state/markers`를 다시 보낸다.

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
| `cmd/nav_cancel` | 주행 취소 |
| `cmd/waypoints/set` | 점검 지점 전체 설정 |
| `cmd/locations/set` | home·dock 전체 설정 |
| `cmd/markers/set` | 마커 전체 설정 |
| `cmd/maps/list` | 로봇 지도 목록 요청 |
| `cmd/maps/select` | `id`로 로봇의 활성 지도 전환 |
| `cmd/maps/rename` | `id`의 로봇 측 표시 이름 변경 |
| `cmd/power/policy` | 배터리 복귀·출발 기준 |
| `cmd/mission/start` | 점검 시작 |
| `cmd/mission/pause` | 점검 일시정지 |
| `cmd/mission/resume` | 점검 재개 |
| `cmd/mission/stop` | 점검 종료 |
| `cmd/arm/preset` | 암 프리셋 |
| `cmd/arm/joint_goal` | 암 관절 목표 |
| `cmd/arm/ee_goal` | 암 끝단 목표 |
| `cmd/arm/stop` | 암 정지 |
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
- 저장 파일명은 `차량번호_량번호_포인트ID,YYYYMMDDHHMMSS.jpg`이고, 같은
  이름의 `.json`에 로봇좌표·촬영거리·Apriltag ID가 들어간다.
- 원본은 로컬 스풀에 보관하고, NAS 체크섬 검증 뒤 삭제한다.
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
