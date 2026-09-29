# 상태 채널

로봇이 `pub` 로 발행한다. 클라이언트는 받아서 보여 줄 뿐 스스로 정하지 않는다.

발행 주기는 보장이 아니라 목표값이다. 브릿지는 송신 큐가 밀리면 오래된 표본을
버린다 — 상태는 최신 값만 의미가 있고, 밀린 큐를 끝까지 비우면 화면이 과거를
그린다.

| 채널 | 주기 | 이진 본문 |
|---|---|---|
| `state/pose` | 10 Hz | |
| `state/arm` | 10 Hz | |
| `state/nav` | 5 Hz | |
| `state/trail` | 2 Hz | |
| `state/battery` | 1 Hz | |
| `state/system` | 1 Hz | |
| `state/health` | 1 Hz | |
| `state/capture_spool` | 1 Hz 및 촬영 시 | |
| `state/safety` | 변경 시 + 1 Hz | |
| `state/base` | 변경 시 | |
| `state/mission` | 변경 시 | |
| `state/waypoints` | 변경 시 | |
| `state/locations` | 변경 시 | |
| `state/markers` | 변경 시 | |
| `state/maps` | 변경 시 | |
| `state/active_map` | 변경 시 | |
| `state/plan` | 변경 시 | |
| `state/apriltag` | 검출 시 | |
| `evt/log` | 이벤트 (`t` 는 `evt`) | |
| `map/occupancy` | 변경 시 | PNG |
| `capture/preview` | 촬영 시 | PNG |

---

## `state/pose` — 위치와 속도

```json
{"x": 1.24, "y": -0.38, "theta": 0.15, "frame": "map",
 "speed": 0.32, "yaw_rate": 0.05, "moving": true}
```

| 필드 | 타입 | 설명 |
|---|---|---|
| `x`, `y` | number | `frame` 기준 위치, 미터 |
| `theta` | number | 방향, 라디안 |
| `frame` | string | 기준 좌표계. 보통 `"map"` |
| `speed` | number | 선속도, m/s |
| `yaw_rate` | number | 각속도, rad/s |
| `moving` | bool | 로봇 판정. 촬영 가능 여부와 직결 |

**`moving` 은 클라이언트가 계산하지 않는다.** 오도메트리가 1초 이상 끊기면
로봇은 `moving: true` 로 본다 — 모르는 채로 찍어 흔들린 사진을 남기는 것보다
거절하는 편이 낫다. `speed` 와 `yaw_rate` 는 오도메트리가 신선하지 않으면
0 으로 나가므로, 이 값만 보고 정지를 판단하면 안 된다.

## `state/safety` — E-Stop 과 모드

```json
{"estop": false, "mode": "auto"}
```

| 필드 | 값 |
|---|---|
| `estop` | `true` 면 발동 중 |
| `mode` | `"auto"` 또는 `"manual"` |

## `state/base` — 본체 자세와 모션 권한

```json
{"posture": "balance_stand", "motion_authority": "none"}
```

| 필드 | 값 |
|---|---|
| `posture` | `stand_up` · `stand_down` · `balance_stand` · `recovery_stand` · `damp` · `unknown` |
| `motion_authority` | `none` · `base_active` · `base_stopping` · `arm_active` · `arm_stopping` |

`posture` 는 로봇이 마지막으로 성공한 자세다. 기동 직후나 외부에서 자세를 바꾼
뒤에는 `unknown` 이다 — 브릿지가 모르는 것을 아는 척하지 않는다.

`motion_authority` 는 본체와 로봇팔 중 어느 쪽이 움직일 권한을 갖는지다. 둘은
동시에 움직이지 않는다.

## `state/nav` — 자율주행

```json
{"status": "navigating", "goal": {"x": 3.0, "y": 1.5},
 "distance_remaining_m": 2.4, "eta_s": 6.1, "current_waypoint_id": null}
```

`status` 값: `idle`, `navigating`, `succeeded`, `failed`, `rejected`, `cancelled`.

`current_waypoint_id` 는 현재 항상 `null` 이다. 경유점 개념이 브릿지에 없다.
필드를 빼지 않고 `null` 로 보내는 것은 클라이언트가 "키 없음" 과 "값 없음" 을
구분하지 않아도 되게 하려는 것이다.

## `state/battery`

```json
{"soc": 72.5, "voltage": 52.1, "current": -8.3, "charging": false}
```

`soc` 는 퍼센트, `voltage` 는 볼트, `current` 는 암페어(방전 시 음수)다.

## `state/system`

```json
{"cpu_pct": 41.2, "mem_pct": 55.0, "cpu_temp_c": 62.0,
 "gpu_pct": 18.0, "gpu_temp_c": 58.0,
 "net_rtt_ms": 3.2, "net_rssi": -47,
 "robot_id": "R1", "robot_name": "1호기",
 "capture_enabled": true, "arm_execution_enabled": false}
```

사람이 읽을 로봇 이름이 실리는 유일한 채널이다.
`capture_enabled`와 `arm_execution_enabled`는 로봇 브리지가 보고하는 기능 활성 여부다.
HMI는 비활성 기능의 실행 버튼을 열지 않는다. 팔 자세 미리보기와 저장은 별개다.

## `state/arm`

```json
{"names": ["j1","j2","j3","j4","j5","j6"],
 "positions": [0.0, -0.3, 1.2, 0.0, 0.9, 0.0],
 "velocities": [0.0, 0.0, 0.0, 0.0, 0.0, 0.0]}
```

관절각은 라디안이다. 세 배열의 길이와 순서는 같다.
`manipulability`·`sigma_min`·`moveit_state`는 실제 제어기가 보고할 때만 보낸다.
없으면 HMI는 조작성 경고나 동작 상태를 추측하지 않는다.

## `state/mission` — 점검 시나리오

```json
{"state": "running", "index": 3, "total": 12}
```

| 값 | 뜻 |
|---|---|
| `idle` | 대기 |
| `running` | 점검 중 |
| `paused` | 일시정지 (조작자 요청·통신 단절·수동 전환) |
| `returning` | 충전 스테이션으로 복귀 중 |
| `completed` | 전체 점검 완료 |
| `fault` | 실패로 멈춤. 명시적 복구 필요 |
| `emergency_stopped` | E-Stop. 해제하면 `paused` 로 내려온다 |

**모르는 값은 `fault` 로 다룬다.** `idle` 로 접으면 화면이 "아무 일도 없음" 으로
보이는데, 실제로는 로봇이 무엇을 하는지 모르는 상태다.

상태는 로봇의 FSM 이 소유한다. 클라이언트가 추측해서 그리면 두 쪽이 갈린다.

## `state/waypoints` — 점검 지점

```json
{"points": [{"id": "P03", "x": 2.1, "y": 0.4, "theta": 1.57, "status": "done"}]}
```

목록 전체가 매번 온다. 지점의 이름·좌표는 로봇에 저장되지만 `status`는 저장하지
않는다. 브리지가 현재 미션 상태에서 계산해 화면에 보낼 때만 붙인다.

## `state/locations` — home 과 dock

```json
{"locations": [{"kind": "dock", "x": 0.0, "y": 0.0, "theta": 0.0},
               {"kind": "home", "x": 1.0, "y": 0.0, "theta": 0.0}]}
```

`kind` 는 `"dock"` 또는 `"home"`. 점검 경로에 포함되지 않는다.

## `state/markers` — 측량된 AprilTag 자리

```json
{"markers": [{"id": 7, "x": 3.2, "y": 1.1, "theta": 0.0}]}
```

지금 보이는 태그가 아니라 **태그를 어디에 붙였는지**의 지도다. 실시간 검출은
`state/apriltag` 다.

## `state/maps` — 보관 중인 지도 목록

```json
{"maps": [{"id": "inspection_a", "name": "inspection_a", "active": true,
           "waypoint_count": 12, "created_at": "2026-09-07T10:22:00Z"}]}
```

| 필드 | 설명 |
|---|---|
| `id` | 지도 폴더 이름. `cmd/maps/select`에 그대로 쓴다. 이름을 바꾸면 함께 바뀐다 |
| `name` | HMI 표시 이름. 현재는 폴더 이름과 같다 |
| `active` | 현재 쓰는 지도인지 |
| `default` | 다음 기동에 자동으로 열 기본 지도인지 |
| `waypoint_count` | 그 지도에 등록된 점검포인트 수 |
| `created_at` | 생성 시각. 없을 수 있다 |

로봇이 보관 중인 지도만 나온다. 관제가 목록을 만들지 않는다.

## `state/active_map` — 현재 지도

```json
{"id": "inspection_a", "name": "inspection_a"}
```

`map/occupancy` 의 `map_id` 와 같은 값이다. 둘이 다르면 지도가 바뀌는 중이다.

## `state/plan`, `state/trail`

```json
{"points": [{"x": 1.0, "y": 0.0}, {"x": 1.2, "y": 0.1}]}
```

`state/plan` 은 계획 경로, `state/trail` 은 지나온 궤적이다. `state/trail` 에는
`reset` 이 함께 오며, `true` 면 클라이언트는 기존 궤적을 버리고 새로 그린다.
새 주행 목표가 수락되거나 새 미션이 실제로 시작될 때, 또는 2D 초기 위치를 다시
지정할 때 경로를 초기화한다. 미션 단계 이동·일시정지·재개·완료는 초기화하지 않는다.
초기 위치 재지정 후에는 새 AMCL 위치가 TF에 반영될 때까지 기록을 보류한다.

## `state/health` — 센서와 링크

```json
{"sensors": [{"id": "lidar", "name": "LiDAR", "expected_hz": 10.0,
              "actual_hz": 9.6, "last_seen_ms": 104,
              "state": "ok", "detail": ""}],
 "link": {"rx_bytes_per_s": 18240, "tx_bytes_per_s": 3120}}
```

| `state` | 조건 |
|---|---|
| `ok` | 정상 |
| `degraded` | 오고는 있으나 기대 주기의 60% 미만 |
| `lost` | 한 번도 못 봤거나 기대 주기의 3배 동안 없음 |

**판정은 로봇이 한다.** 클라이언트는 어떤 센서가 있어야 하는지 모르고 알 필요도
없다. 목록 자체가 "이 로봇에 있어야 할 센서" 이며, 아직 연결되지 않은 것도
`lost` 로 나온다 — 줄이 없으면 원래 없는 것인지 죽은 것인지 구분할 수 없다.

`last_seen_ms` 는 한 번도 못 본 센서에서 `null` 이다.

## `state/capture_spool` — 촬영 공유 저장소

```json
{"nas_online": true, "pending": 0, "spool_free_mb": 18240.5}
```

`nas_online`은 설정된 NAS 마운트가 로봇에서 확인되는지 나타낸다.
`spool_free_mb`는 해당 저장소의 남은 용량이다. 기존 키 이름은 호환성을 위해
유지하지만, 원본을 공유 폴더에 직접 저장하므로 `pending`은 항상 0이다.

## `map/occupancy` — 점유격자

헤더에 메타데이터, payload 에 PNG.

```json
{"width": 1024, "height": 768, "resolution": 0.05,
 "origin": {"x": -25.6, "y": -19.2, "theta": 0.0},
 "map_id": "2026-09-07", "encoding": "png"}
```

`resolution` 은 픽셀당 미터, `origin` 은 지도 좌하단의 map 좌표다.

## `capture/preview` — 촬영 미리보기

헤더에 메타데이터, payload 에 PNG. 촬영과 함께 저장되는 사이드카 JSON 과
**같은 내용**이다.

```json
{"file": "GTXA-042_05_C01-P03,20260906140321.png",
 "vehicle_number": "GTXA-042", "car_number": "05", "point_id": "C01-P03",
 "captured_at": "20260906140321",
 "robot": {"x": 2.1, "y": 0.4, "theta": 1.57},
 "distance_mm": 412.0, "tag_id": 7, "map_id": "2026-09-07"}
```

`distance_mm` 은 정렬된 깊이 영상 중앙값이며, 측정하지 못하면 `null` 이다.
`tag_id` 는 요청에 실려 온 값이고 없으면 `null` 이다.

미리보기는 저장된 그 바이트를 그대로 보낸다. 다시 인코딩하면 화면에 보이는 것과
저장된 것이 달라진다.

## `evt/log` — 이벤트

`t` 가 `evt` 이므로 구독과 무관하게 전달된다.

```json
{"code": "LINK_FRAME_CORRUPT", "level": "error", "msg": "..."}
```

`code` 는 [errors.md](errors.md) 의 카탈로그에 등록된 값이다. 런타임에
조립한 코드는 오지 않는다 — 클라이언트가 설명하지 못하는 코드가 생기지
않게 하기 위해서다.

`level` 은 `info`, `warn`, `error`.
