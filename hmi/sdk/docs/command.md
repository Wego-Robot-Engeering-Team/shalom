# 명령 데이터

모든 명령은 `req`·`res`를 사용한다. 성공 응답의 `p.ok`는 bool이다.
오류는 `p.err.code`·`p.err.msg`로 확인한다.

```json
{"v":1,"t":"res","ch":"cmd/goto","id":"c1","ts":1791302400.0,"robot":"SE-0001","p":{"ok":true}}
```

`cmd/goto`의 성공은 목표 수락, `cmd/nav_pause`·`cmd/nav_cancel`의 성공은 정지 요청 접수다.
도착·정지 완료는 `state/nav`로 확인한다.
목록 조회 응답에는 목록이 포함되지 않으며 해당 `state/*`에서 수신한다.

---

## 안전·본체

| 채널 | p |
| --- | --- |
| `cmd/estop` | `{}` |
| `cmd/estop_release` | `{}` |
| `cmd/mode` | `{"mode":"auto"}` 또는 `{"mode":"manual"}` |
| `cmd/base/posture` | `{"posture":"balance_stand","confirm":false}` |

`posture`: `stand_up`, `stand_down`, `balance_stand`, `recovery_stand`, `damp`.
`damp`는 `confirm:true`로 요청한다. 실제 자세는 `state/base`로 확인한다.

수동 전환 시 로봇의 주행 출력을 정지시키고 기존 목표를 유지한다.
자율 전환·미션 재개는 로봇의 안전 상태와 실행 정책을 따른다.

## 주행

| 채널 | p |
| --- | --- |
| `cmd/goto` | `{"x":4.2,"y":1.8,"theta":1.57}` |
| `cmd/nav_pause` | `{}` |
| `cmd/nav_resume` | `{}` |
| `cmd/nav_cancel` | `{}` |
| `cmd/localization/initial_pose` | `{"x":1.0,"y":2.0,"theta":0.5}` |
| `cmd/trail/snapshot` | `{}` |

`x/y`는 map 좌표(m), `theta`는 도착 방향(rad)이다.
`navigate_to`·`navigateTo`를 호출하면 목표 실행 요청을 즉시 전송한다.
출발 확인 UI를 구현할 때는 후보 목표를 application에 보관하고 시작 버튼에서 호출한다.

일시정지는 목표를 유지하고 재개는 그 목표를 다시 실행한다. 취소는 목표를 제거한다.
미션 수행 중 이동은 미션 제어 API를 사용한다.
초기 위치 명령 성공 후 위치 갱신은 `state/pose`에서 확인한다.

### 속도

```json
{"speed_limit_mps":0.30,"angular_speed_limit_rps":0.50}
```

`cmd/navigation/speed_limit`의 선속도·각속도 제한이다.

```json
{"min_speed_mps":0.10,"max_speed_mps":0.60,
 "min_angular_speed_rps":0.05,"max_angular_speed_rps":0.80}
```

`cmd/navigation/speed_settings`의 범위 설정이다.
현재 설정값은 새 범위 안으로 조정된다.

| 구분 | 허용 범위 |
| --- | --- |
| 선속도 | `0.10 ≤ min ≤ limit ≤ max ≤ 0.60` m/s |
| 각속도 | `0.05 ≤ min ≤ limit ≤ max ≤ 0.80` rad/s |

로봇에 저장된 값은 `state/navigation_speed`로 확인한다.
`autonomous_applied:true`는 Nav2에 해당 제한이 적용된 상태다.

---

## 지도

| 채널 | p | 확인 채널 |
| --- | --- | --- |
| `cmd/maps/list` | `{}` | `state/maps` |
| `cmd/maps/select` | `{"id":"inspection_map"}` | `state/active_map`·`map/occupancy` |
| `cmd/maps/rename` | `{"id":"old_map","name":"new_map"}` | `state/maps`·`state/active_map` |
| `cmd/maps/delete` | `{"id":"old_map"}` | `state/maps` |
| `cmd/maps/set_default` | `{"id":"inspection_map"}` | `state/maps` |

지도 ID는 로봇의 지도 폴더명이다. 이름 변경은 폴더와 관련 참조를 갱신한다.
기본 지도 해제는 `{"id":""}`로 요청한다.
실행 중인 주행·미션과 현재·기본·기동 지도 조건에 따라 변경·삭제가 제한된다.

지도 전환 시 활성 지도와 새 목록·이미지를 함께 확인한다.
지도 선택 응답만으로 이전 지도 목록을 새 지도에 저장하지 않는다.

## 웨이포인트·고정 위치·마커

목록 교체는 편집 시작 시 지도 ID와 원본 목록을 함께 보낸다.
빈 배열은 해당 목록 전체 삭제 요청이다.

```json
{
  "map_id":"inspection_map",
  "expected_points":[{"id":"wp-a","name":"A","x":1.0,"y":2.0,"theta":0.0}],
  "points":[{"id":"wp-a","name":"점검 A","x":1.0,"y":2.0,"theta":1.57,"description":"차량 왼쪽"}]
}
```

| 채널 | 변경 목록 | 원본 목록 |
| --- | --- | --- |
| `cmd/waypoints/set` | `points`: object[] | `expected_points`: object[] |
| `cmd/locations/set` | `locations`: object[] | `expected_locations`: object[] |
| `cmd/markers/set` | `markers`: object[] | `expected_markers`: object[] |

SDK 편의 함수는 세 API 모두 `map_id`와 원본 목록을 요구한다.
웨이포인트 상태의 `status`는 전송용 진행 표시다. SDK는 저장 목록과 원본 비교에서 이 필드를 제거한다.
다른 필드·설명은 원본에 포함한 채 비교한다.

| 항목 | 필드 |
| --- | --- |
| Waypoint | `id:string`, `name:string`, `x/y/theta:float64`, `description:string` |
| Location | `kind:string` (`home`·`dock`), `x/y/theta:float64` |
| Marker | `id:int` (0–100000), `x/y/z/yaw:float64`, `description:string` |

Waypoint ID와 Marker ID는 목록 안에서 고유하다. Location kind도 고유하다.
Marker의 `z`는 태그 중심 높이(m), `yaw`는 지도 +X에서 앞면 바깥쪽 법선까지의 각도(rad, −π…π)다.
구형 X/Y 마커도 읽으며, 3D 마커는 z와 yaw를 함께 등록한다.
미션이 참조하는 웨이포인트·팔 자세의 삭제는 로봇에서 검사한다.

---

## 미션 정의·실행

| 채널 | p |
| --- | --- |
| `cmd/missions/list` | `{}` |
| `cmd/missions/save` | `{"mission":{...},"expected_revision":0}` |
| `cmd/missions/archive` | `{"id":"inspection-001","map_id":"inspection_map","expected_revision":1}` |
| `cmd/mission/start` | `{"mission_id":"inspection-001"}` |
| `cmd/mission/pause`·`resume`·`stop`·`return_dock` | `{}` |

새 미션은 expected_revision 0, 수정은 편집 시작 시 받은 revision을 사용한다.
저장 정의는 아래 형식이며, map_id는 전송 대상 지도를 명시한다.

```json
{
  "id":"inspection-001","name":"차량 점검","map_id":"inspection_map","description":"왼쪽 하부",
  "steps":[
    {"id":"move","type":"navigate","location_id":"wp-a"},
    {"id":"photo","type":"capture","preset":"underbody_left"},
    {"id":"arm","type":"arm_move","pose":"inspection_a"},
    {"id":"return","type":"dock"}
  ]
}
```

| Step type | 참조 |
| --- | --- |
| `navigate` | `location_id:string` — 현재 지도 웨이포인트 ID |
| `capture` | `preset:string` — 촬영 프리셋 ID |
| `arm_move` | `pose:string` — 로봇팔 자세 ID |
| `dock` | 등록된 충전 위치 |

미션과 단계 ID는 ASCII 영문·숫자·`-`·`_`, 1–96자다.
단계 ID는 미션 안에서 고유하며 배열 순서대로 실행한다.
정의 저장·조회와 단계 실행 지원은 구분한다. capture·arm_move 실행 가능 여부는
로봇의 mission_manager와 장치 구성에 따른다.

보관은 `archived:true`로 변경한다. 조회 목록은 `state/missions`,
실행 현황은 `state/mission`이다. `return_dock`는 미션 진행을 보존하며 충전소 복귀를 요청한다.

## 로봇팔

| 채널 | p |
| --- | --- |
| `cmd/arm/pose_presets/list` | `{}` |
| `cmd/arm/pose_presets/save` | `{"preset":{...}}` |
| `cmd/arm/pose_presets/update` | `{"preset":{...},"expected_revision":1}` |
| `cmd/arm/pose_presets/archive` | `{"id":"inspection_a","expected_revision":2}` |
| `cmd/arm/preset` | `{"name":"inspection_a"}` |
| `cmd/arm/joint_goal` | `{"positions":[0.0,-0.4,0.5,-1.2,0.0,0.4]}` |
| `cmd/arm/stop` | `{}` |

```json
{"id":"inspection_a","name":"점검 A","description":"왼쪽 촬영",
 "positions":[0.0,-0.4,0.5,-1.2,0.0,0.4]}
```

관절값은 j1–j6 순서의 rad 6개다. 자세는 지도와 독립적으로 로봇에 저장된다.
저장·수정 결과는 `state/arm_pose_presets`로 확인한다.
실제 실행은 기능 활성 여부·유효한 피드백·정지·권한·안전 조건을 로봇에서 검사한다.
끝단 목표 `cmd/arm/ee_goal`는 MoveIt2 미연동으로 `E_UNREACHABLE`을 반환한다.

## 촬영·배터리

```json
{"vehicle_number":"GTXA-042","train_number":"1234","car_number":"05","point_id":"P1","tag_id":7}
```

`cmd/capture/trigger`는 차량·편성·량·포인트 식별자를 사용한다.
tag_id는 필요할 때 추가한다. 성공 응답의 `p.file`은 저장 파일명이다.
미리보기는 `capture/preview`의 PNG payload, 저장소 상태는 `state/capture_spool`로 확인한다.
촬영은 활성 기능·정지·유효한 영상·저장소 조건을 검사한다.

```json
{"return_at":25,"depart_at":80}
```

`cmd/power/policy`의 배터리 기준(%)이다. SDK는 `0 ≤ return_at < depart_at ≤ 100`을 검사한다.
현재 브리지에서는 실행 중 메모리에 적용하며 재기동 후 다시 설정한다.
