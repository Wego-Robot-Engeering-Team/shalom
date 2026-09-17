# 고객 연동 API v1

SDK `0.2.0`은 HMI와 동일한 TCP `9090`/protocol `v:1` 브릿지에 연결한다.
별도의 숨은 제어 API는 없다. C++17 shared-library API와 Python 3.9+ package가 같은
프레이밍, heartbeat, robot-id 검증, request/response 규칙을 구현한다.

메시지 형식은 [transport.md](transport.md), 상태는 [state.md](state.md), 명령은
[command.md](command.md)에 정의한다.

## 사용 원칙

1. 고객 프로그램은 HMI와 동시에 연결하지 않는다. 현재 브릿지는 한 클라이언트만
   허용한다.
2. 상태는 관측용이며, 안전 판정과 실제 동작 권한은 항상 로봇에 있다.
3. 명령 사용은 계약된 운용 범위에서만 허용한다. 특히 팔 명령은 커미셔닝 전용이며
   고객 운영 프로그램에서 사용하지 않는다.
4. 오류 문장은 표시용이다. 자동 처리에는 `p.err.code`만 사용한다.

## 현재 제한

- `sub` / `unsub`은 현재 수신 기록만 하고, 브릿지는 연결된 client에 모든 상태를 보낸다.
- `cmd/arm/ee_goal`은 `E_UNREACHABLE`로 거절된다.
- `state/nav.current_waypoint_id`는 현재 `null`이며, `state/apriltag`은 검출기 연동 전이다.
- `cmd/cmd_vel` 상한 초과 값은 로봇에서 잘린다. 요청 응답이 없으므로 client가 잘린 값을 알 수 없다.
- 촬영 원본은 NAS로 직접 전송한다. API에는 preview와 spool 상태만 온다.

## C++ API

고객 C++ application의 공개 include 경계는 각 OS의 `cpp/include/`다.

```cpp
#include <shalom/api.hpp>  // 권장: Client와 RobotApi를 함께 제공
```

빌드된 `shalom_api_example`은 `cmd/maps/list`만 보내는 안전한 요청·응답 예제다.
이 예제는 `cpp/examples/api_example.cpp`에 있으며, 제공된 `shalom::sdk` shared
library를 고객 프로젝트에 링크하는 방식과 동일하게 빌드된다.

`shalom::Client`는 연결·5 Hz heartbeat·프레임 수신을 맡고,
`shalom::RobotApi`는 명령별 편의 함수를 제공한다. C++ API의 명령은 **비동기**다.
반환값은 request id이며, 일치하는 `res`와 실제 동작 상태는 `Client::run()`의
콜백에서 확인한다.

```cpp
#include <shalom/api.hpp>

shalom::Client client;
std::string error;
if (!client.connect("192.168.210.88", 9090, &error)) {
    // 연결 오류 처리
}

shalom::RobotApi robot(client);
const std::string id = robot.listMaps(&error);  // cmd/maps/list

client.run([&](const shalom::Message &message) {
    // state/*, evt/*, id가 같은 res를 고객 JSON library로 처리한다.
    return true;
}, &error);
```

`RobotApi`는 `setMode`, `navigateTo`, `cancelNavigation`, `startMission`,
`pauseMission`, `resumeMission`, `stopMission`, `listMaps`, `selectMap`,
`setPowerPolicy`, `setWaypoints`, `setLocations`, `setMarkers`, `triggerCapture`,
`emergencyStop`, `releaseEmergencyStop`, `publishVelocity`를 제공한다.
`armPreset`, `armJointGoal`, `armStop`은 커미셔닝 전용이다.

## Python API

Python의 `Client`는 연결·heartbeat·raw request/response를 맡고, `RobotApi`가
명령별 facade를 맡는다. `request()`는 응답을 기다리는 동안에도
heartbeat를 유지하고, 일치하지 않는 상태·이벤트는 `on_message` callback으로
전달할 수 있다.

```python
from shalom_sdk import Client, RobotApi

client = Client()
client.connect("192.168.210.88")
robot = RobotApi(client)
reply = robot.set_mode("manual")
if not reply.ok:
    print(reply.error_code, reply.error_message)

# 실제 상태는 로봇이 발행한 state/nav, state/mission으로 확인한다.
reply = robot.navigate_to(3.0, 1.5, theta=0.0)
```

Python `RobotApi`는 `emergency_stop`, `release_emergency_stop`, `set_mode`,
`navigate_to`, `cancel_navigation`, `mission_start/pause/resume/stop`,
`list_maps`, `select_map`, `set_power_policy`, `set_waypoints`, `set_locations`,
`set_markers`, `trigger_capture`, `publish_velocity`와 범용 `send_request`/
`request`를 제공한다. `arm_preset`, `arm_joint_goal`, `arm_stop`은
커미셔닝 전용이다.

`publish_velocity`는 응답이 없는 `cmd/cmd_vel` 한 표본을 보낼 뿐이다. 수동 조작을
의도했다면 호출자가 20 Hz로 계속 보내야 하며, 300 ms 동안 끊기면 로봇이 정지한다.

## 최소 연동

각 OS 폴더의 `monitor` 예제를 빌드해 연결·하트비트·프레임 수신을 먼저 확인한다.
그 다음 고객 프로그램에서 `shalom::sdk` 인터페이스 타깃을 링크하거나 C++
`include/` 헤더를 직접 포함한다. C++ JSON 파서는 SDK가 강제하지 않으므로 고객
프로그램의 기존 라이브러리를 사용한다. Python API는 JSON envelope를 `dict`로
제공한다.

## C++ / Python API parity

두 언어는 표기법만 다르고 아래 protocol channel을 같은 의미로 제공한다. C++는
camelCase와 비동기 request id, Python은 snake_case와 동기 `Response`를 사용한다.

| Protocol channel | C++ `RobotApi` | Python `RobotApi` |
| --- | --- | --- |
| `cmd/estop` | `emergencyStop` | `emergency_stop` |
| `cmd/estop_release` | `releaseEmergencyStop` | `release_emergency_stop` |
| `cmd/mode` | `setMode` | `set_mode` |
| `cmd/goto` | `navigateTo` | `navigate_to` |
| `cmd/nav_cancel` | `cancelNavigation` | `cancel_navigation` |
| `cmd/mission/*` | `start/Pause/Resume/StopMission` | `mission_start/pause/resume/stop` |
| `cmd/maps/list`, `cmd/maps/select` | `listMaps`, `selectMap` | `list_maps`, `select_map` |
| `cmd/power/policy` | `setPowerPolicy` | `set_power_policy` |
| `cmd/waypoints/set` | `setWaypoints` | `set_waypoints` |
| `cmd/locations/set` | `setLocations` | `set_locations` |
| `cmd/markers/set` | `setMarkers` | `set_markers` |
| `cmd/capture/trigger` | `triggerCapture` | `trigger_capture` |
| `cmd/cmd_vel` | `publishVelocity` | `publish_velocity` |
| `cmd/arm/preset`, `joint_goal`, `stop` | `armPreset/JointGoal/Stop` | `arm_preset/joint_goal/stop` |
| 모든 향후 `req` channel | `request` | `send_request` / `request` |

## 응답과 실제 결과

`res.p.ok: true`는 **요청 수락**을 뜻한다. 예를 들어 `cmd/goto`의 성공 응답은
Nav2가 목표를 수락했다는 뜻이지 도착했다는 뜻이 아니다. 실제 완료·실패는
`state/nav`, `state/mission`, `state/capture_spool`, `evt/log`로 판단한다.
