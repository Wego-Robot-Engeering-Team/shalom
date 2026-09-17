# 명령 채널

`cmd/cmd_vel` 을 뺀 모든 명령은 `req` / `res` 쌍이다.

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

### `cmd/cmd_vel` — 수동 조작

**유일하게 `pub` 로 보내는 명령이다.** 응답이 없다.

```json
{"vx": 0.3, "vy": 0.0, "wz": 0.2}
```

| 필드 | 단위 | 상한 |
|---|---|---|
| `vx` | m/s | 0.60 |
| `vy` | m/s | 0.40 |
| `wz` | rad/s | 0.80 |

- **20 Hz** 로 계속 보낸다.
- **300 ms** 동안 끊기면 브릿지가 속도를 0 으로 래치한다. 클라이언트가 멈추거나
  링크가 끊겨도 로봇이 계속 달리지 않게 하는 값이다.
- 상한을 넘는 값은 조용히 **잘린다**. 거절되지 않으므로 클라이언트도 같은
  한계를 걸어 두는 편이 좋다 — 다만 그것은 편의이고, 판정은 로봇이 한다.
- E-Stop 중이거나 `auto` 모드면 무시된다.

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
{"vehicle_number": "GTXA-042", "car_number": "05",
 "point_id": "C01-P03", "tag_id": 7}
```

| 필드 | 필수 | 기본값 |
|---|---|---|
| `vehicle_number` | 아니오 | `"UNKNOWN"` |
| `car_number` | 아니오 | `"00"` |
| `point_id` | 아니오 | `"MANUAL"` |
| `tag_id` | 아니오 | `null` |

저장 파일명은 `차량번호_량번호_포인트ID,YYYYMMDDHHMMSS.png` 이고, 같은 이름의
`.json` 에 메타데이터가 함께 저장된다. 성공 여부는 `res.p.ok`, 촬영 결과는
`capture/preview` 및 `state/capture_spool`에서 확인한다.

**이동 중에는 `E_MODE` 로 거절된다.** 과업지시서 2.2.4 가 정지 상태 촬영을
요구하므로, 클라이언트가 버튼을 잠그는 것과 별개로 규칙 자체는 로봇이 지킨다.
오도메트리가 끊겨 속도를 모르면 움직이는 것으로 본다.

카메라 프레임이 없으면 `E_HARDWARE` 다.

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
