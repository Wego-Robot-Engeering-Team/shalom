# 관제 ↔ 로봇 프로토콜 통신 규약 v1

관제와 로봇 브릿지의 TCP 통신 규약이다.

## 전송

- TCP, 기본 포트 `9090`, `TCP_NODELAY` 필수
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
| `state/apriltag` | 마커 검출 |
| `state/mission` | 점검 시나리오 상태 |
| `state/waypoints` | 점검 지점 |
| `state/locations` | home·dock 위치 |
| `state/markers` | 측량된 AprilTag 자리 |
| `state/capture_spool` | 촬영 업로드 상태 |
| `state/health` | 센서·링크 상태 |
| `evt/log` | 이벤트·경고 |
| `map/occupancy` | PNG 점유격자 |
| `capture/preview` | JPEG 미리보기 |

`map/occupancy`는 `width`, `height`, `resolution`, `origin`, `encoding: "png"`을
`p`에 넣고 PNG를 payload로 보낸다. `capture/preview`도 metadata를 `p`에 넣고
이미지를 payload로 보낸다.

## 명령 채널

모든 명령은 `req`/`res`를 쓴다. `cmd/cmd_vel`만 `pub`로 20 Hz 발행한다.
브릿지는 300 ms 동안 `cmd/cmd_vel`을 받지 못하면 0 속도를 발행한다.

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
| `cmd/power/policy` | 배터리 복귀·출발 기준 |
| `cmd/mission/start` | 점검 시작 |
| `cmd/mission/pause` | 점검 일시정지 |
| `cmd/mission/resume` | 점검 재개 |
| `cmd/mission/stop` | 점검 종료 |
| `cmd/arm/preset` | 암 프리셋 |
| `cmd/arm/joint_goal` | 암 관절 목표 |
| `cmd/arm/ee_goal` | 암 끝단 목표 |
| `cmd/arm/stop` | 암 정지 |
| `cmd/capture/trigger` | 촬영 |
| `cmd/cmd_vel` | 수동 속도 (`vx`, `vy`, `wz`) |

## 안전·연결

```text
Nav2 / 브릿지 → /cmd_vel_raw → 안전 게이트 → /cmd_vel → 로봇
```

- E-Stop, 통신 두절, 명령 중재는 로봇의 안전 노드 책임이다.
- 관제와 브릿지는 5 Hz 하트비트를 교환한다.
- 안전 노드는 통신 두절 **3초** 후 정지한다.
- 재연결 후 자율주행은 자동 재개하지 않는다.

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
