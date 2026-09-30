# 로봇 운용 데이터 형식

현재 HMI와 `hmi_bridge`가 사용하는 JSON 형식이다. 아래 `path`는 로봇의
`/var/lib/shalom/`을 기준으로 적었다. 이 경로는 소스 저장소 `shalom/` 밖에
있다. `<map_id>`는 지도 폴더 이름이며 지도별 JSON에는 다시 기록하지 않는다.
실행 시 `maps_dir`로 저장 경로를 변경할 수 있다.
저장 파일에서 `map_id`가 필요한 곳은 기본 지도를 지정하는
`maps/default_map.json`이다.
좌표는 ROS `map` 좌표계의 미터, 각도는 라디안으로 저장한다.
`revision`은 미션과 팔 자세의 수정 번호다.

---

## 웨이포인트

path: `maps/<map_id>/waypoints.json`

```json
{
  "points": [
    {
      "id": "wp-inspect-a",
      "name": "차량 왼쪽 점검",
      "x": 4.2,
      "y": 1.8,
      "theta": 1.5707963267948966,
      "description": ""
    }
  ]
}
```

| 필드 | 타입 | 의미 |
|---|---|---|
| `id` | `string` | 지도 안에서 고유한 식별자. 미션의 `navigate.location_id`가 참조한다. |
| `name` | `string` | HMI 표시 이름. 1–120바이트. |
| `x`, `y` | `float64` | 로봇이 이동할 목표 위치(m). |
| `theta` | `float64` | 도착 방향(rad). 지도 +X가 0, 반시계 방향이 양수. |
| `description` | `string` | 웨이포인트 설명. 비워둘 때 `""`. |

---

## Markers

path: `maps/<map_id>/markers.json`

```json
{
  "markers": [
    {
      "id": 1,
      "x": 4.0,
      "y": 1.5,
      "z": 1.2,
      "yaw": 1.5707963267948966,
      "description": ""
    }
  ]
}
```

| 필드 | 타입 | 의미 |
|---|---|---|
| `id` | `int` | 실제 태그에 인쇄된 번호. 0–100000, 지도 안에서 고유하다. |
| `x`, `y`, `z` | `float64` | `map` 좌표계에서 태그 중심의 3D 위치(m). |
| `yaw` | `float64` | 지도 +X에서 태그 앞면의 바깥쪽 법선까지의 방향(rad, −π…π). |
| `description` | `string` | 마커 설명. 비워둘 때 `""`. |

---

## 로봇팔 관절 자세

path: `maps/arm_pose_presets.json`

```json
{
  "presets": [
    {
      "id": "inspection_a",
      "name": "점검 자세 A",
      "positions": [0.0, -0.4, 0.5, -1.2, 0.0, 0.4],
      "revision": 1,
      "archived": false,
      "description": "",
    }
  ]
}
```

| 필드 | 타입 | 의미 |
|---|---|---|
| `id` | `string` | 로봇 전체에서 고유한 식별자. 미션의 `arm_move.pose`가 참조한다. |
| `name` | `string` | HMI 표시 이름. UTF-8 기준 1–80바이트. 활성 자세끼리 고유하다. |
| `positions` | `float64[6]` | `j1`부터 `j6`까지의 목표 관절각(rad). |
| `revision` | `uint64` | 로봇이 생성·수정·보관할 때 증가시키는 개정 번호. 1부터 시작한다. |
| `archived` | `bool` | `true`면 일반 목록에서 숨긴다. |
| `description` | `string` | 설명. 최대 400바이트. 기본값 `""`. |

### 관절각 범위

브리지는 모든 관절값이 유한한 숫자인지와 아래 범위를 검사한다.

| 관절 | 최소(rad) | 최대(rad) |
|---|---:|---:|
| `j1` | -3.0543 | 3.0543 |
| `j2` | -4.6251 | 1.4835 |
| `j3` | -2.8274 | 2.8274 |
| `j4` | -4.6251 | 1.4835 |
| `j5` | -3.0543 | 3.0543 |
| `j6` | -3.0543 | 3.0543 |

HMI에서 자세를 선택하면 목표 관절값과 3D 미리보기가 갱신된다. `실행`을 누르면
로봇팔에 이동 명령을 보낸다. 자세는 지도와 독립적으로 저장된다.

`state/arm`은 로봇의 실제 관절값 `positions`, 관절명 `names`, 보고된 관절 속도
`velocities`를 전달한다. `cmd/arm/joint_goal`은
`{"positions": [관절각 6개]}` 형식으로 목표값을 보낸다.

---

## 미션

path: `maps/<map_id>/missions.json`

```json
{
  "missions": [
    {
      "id": "inspection-001",
      "name": "차량 하부 점검",
      "revision": 1,
      "archived": false,
      "steps": [
        {"id": "move-1", "type": "navigate", "location_id": "wp-inspect-a"},
        {"id": "photo-1", "type": "capture", "preset": "underbody_left"},
        {"id": "arm-1", "type": "arm_move", "pose": "inspection_a"},
        {"id": "return-1", "type": "dock"}
      ],
      "description": "차량 하부 정기 점검"
    }
  ]
}
```

| 필드 | 타입 | 의미 |
|---|---|---|
| `id` | `string` | 미션 고유 ID. 영문·숫자·`-`·`_`, 1–96자. |
| `name` | `string` | HMI 표시 이름. 1–120바이트. |
| `revision` | `uint64` | 로봇이 수정·보관할 때 증가시키는 개정 번호. 1부터 시작한다. |
| `archived` | `bool` | `true`면 HMI의 일반 목록에서 숨긴다. |
| `steps` | `Step[]` | 배열 순서가 실행 순서다. 단계 `id`는 미션 안에서 고유하다. |
| `description` | `string` | 미션 설명. 비워둘 때 `""`. |

### 단계 공통 필드

| 필드 | 타입 | 의미 |
|---|---|---|
| `id` | `string` | 미션 안에서 고유한 단계 ID. 영문·숫자·`-`·`_`, 1–96자. |
| `type` | `string` | 단계 유형. 값은 아래 표의 네 가지 중 하나. |

### 단계 유형별 데이터

| `type` 값 | 추가 필드 | 값 타입 | 의미 |
|---|---|---|---|
| `"navigate"` | `location_id` | `string` | `waypoints.json`의 웨이포인트 ID. 해당 위치의 `x`, `y`, `theta`로 주행한다. |
| `"capture"` | `preset` | `string` | 촬영 프리셋 ID. |
| `"arm_move"` | `pose` | `string` | `arm_pose_presets.json`의 자세 ID. |
| `"dock"` | `location_id` | `string` | 생략하면 등록된 `dock` 위치를 사용한다. 지정하면 등록된 dock ID와 비교한다. 마지막 단계에 둔다. |

`navigate`·`dock`은 현재 실행할 수 있다. `capture`·`arm_move`는 ID 검증과
저장까지 구현되어 있으며, 실행 요청 시 미션 시작을 거절한다.

---

## 충전 스테이션

path: `maps/<map_id>/locations.json`

```json
{
  "locations": [
    {"kind": "dock", "x": 0.5, "y": -1.0, "theta": 3.141592653589793}
  ]
}
```

| 필드 | 타입 | 의미 |
|---|---|---|
| `kind` | `string` | `dock`(충전 위치) 또는 `home`(시작 위치). |
| `x`, `y` | `float64` | 지도 좌표(m). |
| `theta` | `float64` | 로봇 방향(rad). |

---

## 저장과 동기화

- HMI는 로봇 브리지에 명령을 보내고, 저장 성공 후 로봇이 다시 발행한 목록을
  표시한다. 데이터의 원본은 연결된 로봇 또는 시뮬레이션의 `<maps_dir>`이다.
- 미션과 팔 자세를 HMI에서 수정할 때는 마지막으로 받은 `revision`을
  `expected_revision`으로 보낸다. 다른 화면이 먼저 바꿨다면 저장이 거절된다.
- JSON 파일을 직접 수정한 뒤에는 브리지를 재시작하거나 지도를 다시 불러온다.
- 웨이포인트·마커·미션의 `description`은 현재 JSON 파일에서 직접 편집한다.
