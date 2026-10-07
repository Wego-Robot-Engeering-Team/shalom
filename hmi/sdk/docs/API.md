# SDK API

SDK `0.4.0` / protocol `v:1`. 길이·속도·각도는 각각 m, m/s, rad, rad/s를 사용한다.

## Client

| 기능 | Python | C++ |
| --- | --- | --- |
| 연결 | `connect(host, port=9090, timeout_s=5)` | `connect(host, port, err, timeoutMs)` |
| 종료 | `close()`, `with Client()` | `close()`, RAII |
| 수신·하트비트 | `poll(timeout_s=0.2)` | `poll(messages, timeoutMs=200, err)` |
| 수신 루프 | `run(handler)` | `run(handler, err)` |
| 최신 상태 복사 | `latest(channel)` → `Message / None` | `latest(channel)` → `optional<Message>` |
| 비동기 요청 | `send_request(channel, payload, timeout_s)` → `str` | `sendRequest(channel, payloadJson, err, timeoutMs)` → `string` |
| 비동기 응답 소비 | `take_response(id)` | `takeResponse(id)` |
| 대기 기록 제거 | `forget_request(id)` | `forgetRequest(id)` |
| 동기 요청 | `request(channel, payload, timeout_s, on_message)` → `Response` | `request(channel, payloadJson, timeoutMs, handler, err)` → `optional<Response>` |

한 Client는 한 스레드에서 사용한다. 콜백은 빠르게 처리하며 콜백 안에서는
`send_request`·`sendRequest`를 사용한다. `poll(0)`은 대기 없이 수신을 시도한다.

비동기 반환 ID는 전송 결과다. 같은 ID·채널의 `res.p.ok`가 명령 응답이며,
주행 도착·미션 완료는 `state/nav`·`state/mission`에서 확인한다.
로컬 대기 기록 제거는 로봇 동작을 취소하지 않는다.

Python `Response`: `request_id`, `ok`, `error_code`, `error_message`, `data`, `message`.
C++ `Response`: `requestId`, `ok`, `errorCode`, `errorMessage`, `message`.
C++ `nullopt`는 전송·연결·프로토콜·타임아웃 실패이고 `err`에 사유가 기록된다.
Python은 해당 상황에서 `ClientError`의 하위 예외를 발생시킨다.

Python `Message.envelope`는 dict, `payload`는 bytes, `age_s`는 수신 후 경과 시간이다.
C++ `Message.envelope`는 JSON string, `payload`는 string이며 `type`·`channel`·`requestId`가
파싱되어 제공된다. `receivedMonotonicSeconds`는 수신 단조 시각이다.

`latest()`는 현재 연결에서 받은 상태를 반환한다. 상태의 신선도는 수신 시각과
채널 주기로 확인한다. 연결 종료 시 상태·로봇 ID·대기 요청을 초기화한다.
자동 재연결·자동 재실행은 제공하지 않는다.

## RobotApi

Python은 `RobotApi(client)`, C++는 `robot_sdk::RobotApi robot(client)`를 사용한다.
Python 편의 함수는 동기 Response, C++ 편의 함수는 비동기 요청 ID를 반환한다.

| 채널 | Python | C++ |
| --- | --- | --- |
| `cmd/estop` | `emergency_stop` | `emergencyStop` |
| `cmd/estop_release` | `release_emergency_stop` | `releaseEmergencyStop` |
| `cmd/mode` | `set_mode` | `setMode` |
| `cmd/base/posture` | `set_base_posture` | `setBasePosture` |
| `cmd/goto` | `navigate_to` | `navigateTo` |
| `cmd/nav_pause` | `pause_navigation` | `pauseNavigation` |
| `cmd/nav_resume` | `resume_navigation` | `resumeNavigation` |
| `cmd/nav_cancel` | `cancel_navigation` | `cancelNavigation` |
| `cmd/localization/initial_pose` | `set_initial_pose` | `setInitialPose` |
| `cmd/navigation/speed_limit` | `set_speed_limits` | `setSpeedLimits` |
| `cmd/navigation/speed_settings` | `set_speed_ranges` | `setSpeedRanges` |
| `cmd/trail/snapshot` | `trail_snapshot` | `trailSnapshot` |
| `cmd/mission/start` | `mission_start(mission_id)` | `startMission(missionId)` |
| `cmd/mission/pause` | `mission_pause` | `pauseMission` |
| `cmd/mission/resume` | `mission_resume` | `resumeMission` |
| `cmd/mission/stop` | `mission_stop` | `stopMission` |
| `cmd/mission/return_dock` | `return_to_dock` | `returnToDock` |
| `cmd/missions/list` | `list_missions` | `listMissions` |
| `cmd/missions/save` | `save_mission` | `saveMission` |
| `cmd/missions/archive` | `archive_mission` | `archiveMission` |
| `cmd/maps/list` | `list_maps` | `listMaps` |
| `cmd/maps/select` | `select_map` | `selectMap` |
| `cmd/maps/rename` | `rename_map` | `renameMap` |
| `cmd/maps/delete` | `delete_map` | `deleteMap` |
| `cmd/maps/set_default` | `set_default_map` | `setDefaultMap` |
| `cmd/waypoints/set` | `set_waypoints` | `setWaypoints` |
| `cmd/locations/set` | `set_locations` | `setLocations` |
| `cmd/markers/set` | `set_markers` | `setMarkers` |
| `cmd/power/policy` | `set_power_policy` | `setPowerPolicy` |
| `cmd/capture/trigger` | `trigger_capture` | `triggerCapture` |
| `cmd/arm/preset` | `arm_preset` | `armPreset` |
| `cmd/arm/joint_goal` | `arm_joint_goal` | `armJointGoal` |
| `cmd/arm/stop` | `arm_stop` | `armStop` |
| `cmd/arm/pose_presets/list` | `list_arm_poses` | `listArmPoses` |
| `cmd/arm/pose_presets/save` | `save_arm_pose` | `saveArmPose` |
| `cmd/arm/pose_presets/update` | `update_arm_pose` | `updateArmPose` |
| `cmd/arm/pose_presets/archive` | `archive_arm_pose` | `archiveArmPose` |

Python 목록 입력은 object 배열이다. C++는 `Waypoint`·`Location`·`Marker` 배열과
편집 전 목록의 JSON array string을 사용한다. C++ 미션·팔 자세 저장 함수의 입력은
JSON object string이다. 상세 필드는 [명령](command.md)에 정의한다.

C++ `Marker.pose.x/y`는 지도 좌표, `pose.theta`는 태그 앞면 법선 yaw, `z`는 높이다.
전송 필드는 `x/y/z/yaw`이다.

## 저장 예제

편집을 시작할 때 지도와 원본 목록을 복사하고, 수정 후 그 원본을 그대로 보낸다.

```python
from copy import deepcopy

snapshot = client.latest("state/waypoints")
active = client.latest("state/active_map")
if snapshot is not None and active is not None:
    map_id = active.envelope["p"]["id"]
    original = deepcopy(snapshot.envelope["p"]["points"])
    edited = deepcopy(original)
    if edited:
        edited[0]["name"] = "점검 위치 A"
        reply = robot.set_waypoints(
            edited, map_id=map_id, expected_points=original
        )
```

`E_BUSY`가 반환되면 최신 목록을 다시 확인하고 변경 내용을 비교한다.
미션·팔 자세는 편집 시작 시 받은 revision을 유지한다.
저장 응답과 갱신 목록은 어느 순서로든 도착할 수 있다.

## 0.3 → 0.4 변경

- OS별 `<OS>/python`·`<OS>/cpp` 경로를 사용한다.
- 미션 시작: 실행할 미션 ID를 전달한다.
- 목록 교체: 지도 ID와 편집 전 목록을 전달한다.
- 촬영: `train_number`·`trainNumber`를 추가한다.
- 미지원 수동 속도 편의 함수와 `Twist2D`를 제거했다.
- C++ 프로그램은 새 헤더·라이브러리로 재빌드한다.
