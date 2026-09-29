# 명령 채널

모든 고객 SDK 명령은 `req` / `res` 쌍이다.

```json
→ {"v":1,"t":"req","ch":"cmd/goto","id":"c17","ts":...,"p":{"x":3.0,"y":1.5}}
← {"v":1,"t":"res","ch":"cmd/goto","id":"c17","ts":...,"robot":"R1",
   "p":{"ok":true}}
```

실패하면:

```json
← {"p":{"ok":false,"err":{"code":"E_MODE","msg":"수동 모드에서는 ..."}}}
```

`id` 는 클라이언트가 만들고 로봇이 그대로 되돌린다. 응답을 기다리지 않고 다음
명령을 보내도 되지만, 같은 대상에 대한 중복 요청은 `E_BUSY` 로 거절될 수 있다.

**모든 제한은 로봇이 건다.** 클라이언트가 버튼을 잠그는 것과 별개로 규칙 자체는
로봇이 지킨다. UI 결함이나 손으로 짠 클라이언트는 화면의 제한을 그냥 통과한다.

---

## 안전

### `cmd/estop` — 비상정지 발동

페이로드 없음. 언제나 받아들여진다.

### `cmd/estop_release` — 수동 해제

페이로드 없음. 해제하면 미션은 `emergency_stopped` 에서 `paused` 로 내려온다.
자율주행이 자동으로 재개되지는 않는다.

### `cmd/mode` — 운용 모드

```json
{"mode": "manual"}
```

`"auto"` 또는 `"manual"`. 수동 모드에서는 `cmd/goto` 가 `E_MODE` 로 거절된다.

**수동으로 바꿔도 진행 중인 자율주행이 취소되지는 않는다.** 대신 로봇이 수동
쪽에 우선권을 주어 자율 출력이 바퀴까지 가지 못하게 잡아 둔다. 다시 `auto` 로
바꾸면 하던 주행이 이어진다 — 잠깐 비켜 세우려는 조작과 점검을 접는 조작을
같은 버튼에 묶지 않기 위해서다. 정말로 취소하려면 `cmd/nav_cancel` 을 쓴다.

---

## 주행

### `cmd/goto` — 목표 자세

```json
{"x": 3.0, "y": 1.5, "theta": 0.0}
```

| 필드 | 필수 | 설명 |
|---|---|---|
| `x`, `y` | **예** | map 좌표, 미터. 없거나 숫자가 아니면 `E_BAD_PAYLOAD` |
| `theta` | 아니오 | 목표 방향, 라디안. 기본 `0.0` |

거절 사유:

| 코드 | 상황 |
|---|---|
| `E_MODE` | 수동 모드 |
| `E_BUSY` | 직전 목표를 아직 처리 중 |
| `E_UNREACHABLE` | Nav2 미준비, 목표 거부, 또는 응답 없음 |

응답은 Nav2 가 목표를 **수락한 시점**에 온다. 도착 여부가 아니다. 진행은
`state/nav` 로 관측한다.

### `cmd/nav_cancel` — 주행 취소

페이로드 없음.

- 상한을 넘는 값은 조용히 **잘린다**. 거절되지 않으므로 클라이언트도 같은
  한계를 걸어 두는 편이 좋다 — 다만 그것은 편의이고, 판정은 로봇이 한다.
- E-Stop 중이면 무시된다.
- **`auto` 모드에서도 받는다.** 보내는 동안에는 수동이 자율보다 앞서고, 멈추면
  300 ms 뒤 로봇이 하던 자율주행으로 돌아간다. 자율 중에 사람이 잠깐 비켜
  세우는 조작이 모드 전환 없이 되도록 한 것이다.
- `manual` 모드에서는 보내지 않는 동안 로봇이 스스로 제자리 명령을 유지한다.
  클라이언트가 0 을 계속 보낼 필요는 없다 — 링크가 끊겨도 로봇이 자율로
  돌아가지 않도록 그 판단을 로봇에 두었다.

---

## 미션

전부 페이로드가 없다.

| 채널 | 동작 |
|---|---|
| `cmd/mission/start` | 점검 시작 |
| `cmd/mission/pause` | 일시정지 |
| `cmd/mission/resume` | 재개 — 명시적으로만 |
| `cmd/mission/stop` | 종료. `fault` 에서 빠져나올 때도 쓴다 |

상태 전이는 로봇의 FSM 이 판단한다. 현재 상태에서 불가능한 전이는 `E_MODE` 로
거절된다. 결과는 `state/mission` 으로 확인한다.

### `cmd/power/policy` — 배터리 기준

```json
{"return_at": 25, "depart_at": 80}
```

퍼센트. 설정이 바뀔 때와 연결 직후에 보낸다. 로봇은 마지막으로 받은 값을
보관하며 내장 기본값으로 조용히 되돌아가지 않는다 — 그러면 조작자가 로봇이
쓰지 않는 숫자를 보게 된다.

---

## 본체 자세

### `cmd/base/posture`

```json
{"posture": "stand_down"}
```

| 값 | 동작 |
|---|---|
| `stand_up` | 일어서기 |
| `stand_down` | 앉기 |
| `balance_stand` | 균형 서기 — 주행 준비 자세 |
| `recovery_stand` | 넘어졌을 때 복구 |
| `damp` | 관절 힘 빼기. **로봇이 주저앉는다** |

거절 사유:

| 코드 | 상황 |
|---|---|
| `E_BAD_PAYLOAD` | 지원하지 않는 자세 이름 |
| `E_ESTOP_ENGAGED` | 비상정지 중 |
| `E_BUSY` | 로봇팔이 동작 중 |
| `E_MODE` | 이동 중 `stand_down`·`damp`, 또는 `damp` 에 확인 누락 |
| `E_HARDWARE` | 본체 드라이버 무응답, 또는 본체가 전환을 거부 |

**로봇팔이 움직이는 동안에는 거절된다.** 팔이 펴진 채 앉으면 차체나 바닥에
부딪힌다. 클라이언트가 버튼을 잠그는 것과 별개로 판정은 로봇이 한다.

**`damp` 은 확인이 필요하다.**

```json
{"posture": "damp", "confirm": true}
```

관절 힘을 빼는 명령이라 서 있는 상태에서 실행하면 로봇이 주저앉는다. 실수로
누르는 것을 막기 위해 `confirm` 없이는 거절한다.

응답은 본체가 전환을 **마친 뒤**에 온다. 성공 응답에는 `{"ok": true}` 만 실리므로
바뀐 자세는 뒤이어 오는 `state/base` 에서 읽는다.

---

## 팔

### `cmd/arm/preset`

> **커미셔닝 전용.** 이 명령과 아래 `joint_goal`은 브릿지에서 관절 명령을
> 발행하지만, 납품 구성의 실제 팔 제어 권한은 별도 승인 전까지 활성화하지 않는다.
> 고객 운영 프로그램에서 사용하면 안 된다.

```json
{"name": "stow"}
```

### `cmd/arm/joint_goal`

```json
{"positions": [0.0, -0.3, 1.2, 0.0, 0.9, 0.0]}
```

관절각, 라디안. 길이는 `state/arm` 의 `names` 와 같아야 한다.

### `cmd/arm/ee_goal` — 끝단 목표

**현재 구현되지 않았다.** `E_UNREACHABLE` 로 거절된다. 역기구학 판단이
MoveIt2 의 몫인데 아직 연동되지 않았다.

### `cmd/arm/stop`

페이로드 없음. 현재 자세를 유지한다.

---

## 촬영

### `cmd/capture/trigger`

```json
{"vehicle_number": "GTXA-042", "train_number": "1234", "car_number": "05",
 "point_id": "C01-P03", "tag_id": 7}
```

| 필드 | 필수 | 기본값 |
|---|---|---|
| `vehicle_number` | 예 | 없음 |
| `train_number` | 예 | 없음 |
| `car_number` | 예 | 없음 |
| `point_id` | 예 | 없음 |
| `tag_id` | 아니오 | `null` |

저장 파일명은 `차량번호_량번호_포인트ID,YYYYMMDDHHMMSS.png` 이고, 같은 이름의
`.json` 에 메타데이터가 함께 저장된다. 성공 응답의 `res.p.ok: true`와
`res.p.file`이 실제 저장 파일명을 알려준다. `capture/preview`에는 저장된
PNG 바이트와 메타데이터가 온다.

**이동 중에는 `E_MODE` 로 거절된다.** 과업지시서 2.2.4 가 정지 상태 촬영을
요구하므로, 클라이언트가 버튼을 잠그는 것과 별개로 규칙 자체는 로봇이 지킨다.
오도메트리가 끊겨 속도를 모르면 움직이는 것으로 본다.

카메라 프레임이 없으면 `E_UNREACHABLE`, NAS 마운트가 없으면 `E_HARDWARE`다.

촬영 직후 `capture/preview` 와 `state/capture_spool` 이 발행된다.

---

## 지도

### `cmd/maps/list` — 목록 요청

페이로드 없음. 응답 뒤 `state/maps` 가 발행된다.

### `cmd/maps/select` — 지도 전환

```json
{"id": "2026-09-07"}
```

`state/maps` 의 `id` 를 그대로 쓴다.

거절 사유:

| 코드 | 상황 |
|---|---|
| `E_BUSY` | 주행 중이거나 점검이 진행 중 |
| `E_BAD_PAYLOAD` | 없는 지도이거나 `id` 가 유효하지 않음 |

**주행·점검 중에는 바꿀 수 없다.** 지도를 바꾸면 좌표계가 통째로 달라져
진행 중인 목표와 점검포인트가 의미를 잃는다.

전환이 끝나면 `state/active_map`, `map/occupancy`, `state/waypoints`,
`state/locations`, `state/markers` 가 새 지도 기준으로 다시 발행된다.

### `cmd/maps/rename` — 지도 폴더 이름 변경

```json
{"id": "inspection_a", "name": "inspection_b"}
```

`id`는 현재 폴더 이름이고 `name`은 새 폴더 이름이다. 로봇은 폴더와 지도별
웨이포인트·미션의 지도 참조를 함께 변경한다. 이후 `state/maps`와
`state/active_map`은 새 폴더명을 `id`와 `name`으로 보낸다.

주행·점검 중에는 바꿀 수 없다. `name`은 UTF-8 1~120바이트이며 경로 구분자,
제어 문자, `..`를 포함할 수 없다. 이미 존재하는 폴더 이름도 사용할 수 없다.

### `cmd/maps/set_default` — 다음 기동의 기본 지도

```json
{"id": "inspection_a"}
```

로봇의 `maps_dir/default_map.json`을 갱신한다. `id`를 빈 문자열로 보내면 기본
지도를 해제해 다음 기동에 SLAM으로 시작한다. 현재 실행 중인 지도는 바꾸지 않는다.

---

## 설정 — 전체 교체

세 채널 모두 **목록 전체를 대체한다.** 부분 갱신이 아니다.

### `cmd/waypoints/set`

```json
{"points": [{"id": "P03", "x": 2.1, "y": 0.4, "theta": 1.57}]}
```

### `cmd/locations/set`

```json
{"locations": [{"kind": "dock", "x": 0.0, "y": 0.0, "theta": 0.0}]}
```

`kind` 는 `"dock"` 또는 `"home"`.

### `cmd/markers/set`

```json
{"markers": [{"id": 7, "x": 3.2, "y": 1.1, "theta": 0.0}]}
```

성공하면 로봇이 해당 `state/*` 채널을 즉시 재발행한다. 클라이언트는 그것을
받아 화면을 맞춘다.
