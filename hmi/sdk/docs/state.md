# 상태 데이터

브리지의 `pub`를 처리한다. 누락·null 값은 미수신으로 표시하고,
수신 시각을 기준으로 오래된 상태를 구분한다.
주기는 설정에 따라 달라지며 목록·지도 상태는 연결·변경 시 발행된다.

| 채널 | 주요 필드 |
| --- | --- |
| `state/pose` | `x/y/theta`, `frame`, `speed`, `yaw_rate`, `moving` |
| `state/safety` | `estop`, `mode`, 안전 상태 |
| `state/base` | `posture`, `motion_authority` |
| `state/nav` | `status`, `goal`, `distance_remaining_m`, `eta_s`, `elapsed_s`, `recoveries`, `error`, `navigation_state`, `localization_state` |
| `state/navigation_speed` | 선속도·각속도 제한·범위, `autonomous_applied` |
| `state/mission` | `mission_id`, `state`, `index`, `total`, `reason_code`, `detail` |
| `state/missions` | `map_id`, `missions` |
| `state/arm` | `names`, `positions`, `velocities` |
| `state/arm_pose_presets` | `presets` |
| `state/waypoints` | `points` |
| `state/locations` | `locations` |
| `state/markers` | `markers` |
| `state/maps` | `maps` — ID·이름·활성·기본 지도·메타데이터 |
| `state/active_map` | `id`, `name` |
| `state/plan` · `state/trail` | `points`, trail의 `reset` |
| `state/battery` | `soc`, `voltage`, `current`, `charging` |
| `state/system` | 로봇 ID·이름, 시스템 지표, `capture_enabled`, `arm_execution_enabled`, 촬영 정지 기준 |
| `state/health` | `sensors`, `link` |
| `state/capture_spool` | `nas_online`, `pending`, `spool_free_mb` |
| `state/apriltag` | 검출기 구성에 따른 검출 상태 |
| `map/occupancy` | 지도 메타데이터 + PNG binary payload |
| `capture/preview` | 촬영 메타데이터 + PNG binary payload |
| `evt/log` (`t:evt`) | `code`, `level`, 부가 정보 |

## 주행

```json
{
  "status":"navigating","goal":{"x":4.2,"y":1.8,"theta":1.57},
  "distance_remaining_m":2.4,"eta_s":8.0,"elapsed_s":12.0,"recoveries":0,
  "navigation_state":"active","localization_state":"active","error":"",
  "ready_reason_code":"","ready_detail":"","current_waypoint_id":null
}
```

`status`에는 `idle`, `accepting`, `navigating`, `pausing`, `paused`, `canceling`,
`succeeded`, `failed`, `rejected`, `canceled`가 사용된다.
수신한 새 상태값도 보존하여 표시한다.

`navigation_state`·`localization_state`는 lifecycle 상태다.
`active`는 노드 활성 상태이며 위치추정 수렴 판정은 별도다.
진행 피드백이 오래되면 남은 거리·시간 등이 null로 전달될 수 있다.

## 속도

```json
{
  "speed_limit_mps":0.30,"min_speed_mps":0.10,"max_speed_mps":0.60,
  "angular_speed_limit_rps":0.50,"min_angular_speed_rps":0.05,"max_angular_speed_rps":0.80,
  "autonomous_applied":true
}
```

저장 응답과 이 상태를 함께 확인한다. `autonomous_applied`가 true일 때
해당 값이 자율주행 제어기에 적용되어 있다.

## 미션·로봇팔 자세

```json
{"state":"running","mission_id":"inspection-001","index":1,"total":4,
 "reason_code":"","detail":""}
```

`index`는 0부터 시작하는 현재 단계이며 준비 상태에서 −1일 수 있다.
미션 상태에는 `idle`·`ready`·`running`·`pausing`·`paused`·`recovering`·`returning`·
`completed`·`failed`·`emergency_stopped` 등이 있다.

`state/missions`는 저장 정의와 revision·archived를 포함한다.
`state/arm_pose_presets`는 지도와 독립적인 자세 정의를 전달한다.
`state/arm.positions`는 실제 관절 피드백이다. 저장 자세의 목표값과 별도로 관리한다.

## 지도와 목록

지도 전환 시 `state/active_map.id`와 `map/occupancy.p.map_id`를 맞춘다.
`state/missions.map_id`도 소속 지도를 확인하는 데 사용한다.

웨이포인트의 `status`는 `todo`·`current`·`done`·`error` 진행 표시이며
로봇에 저장된 위치 정의와 구분한다.
마커는 `x/y/z/yaw`와 설명을 전달한다. 구형 X/Y 기록에는 z·yaw가 없을 수 있다.

`state/plan`은 목표까지의 계획 경로다.
`state/trail.reset:true`이면 이전 궤적을 지우고 새 points를 사용한다.
새 목표·새 미션 시작·초기 위치 지정 시 초기화되며 미션 내 이동 단계·재개는 유지한다.
늦게 연결한 클라이언트는 `cmd/trail/snapshot`으로 전체 궤적을 요청할 수 있다.

## 바이너리 데이터

`map/occupancy`와 `capture/preview`의 영상은 `Message.payload`에 있다.
JSON envelope의 p에는 지도 크기·해상도·원점 또는 촬영 식별자가 들어 있다.
state 캐시 `latest()`는 `state/*`에 사용한다. 영상은 수신 콜백에서 처리한다.
