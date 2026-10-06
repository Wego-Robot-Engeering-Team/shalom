# safety_manager

ROS 수준의 운용 안전 상태를 하나로 소유한다. 물리 E-stop 회로를 대체하지 않으며,
물리 E-stop 상태는 `/safety/physical_estop_active`로 이 노드에 **통지**돼야 한다.

`safety_manager::core::StateMachine`은 항상 `INITIALIZING`, permit=false로
시작한다. 필수 입력이 준비돼도 `CONTROLLED_STOP`까지만 이동하며, 운영자의 명시적
resume 후에만 `NORMAL`이 된다. ROS 노드와 단위 테스트 모두 이 FSM 하나를 사용한다.

| Input | Type | Meaning |
|---|---|---|
| `/safety/event` | `shalom_interfaces/SafetyEvent` | 내부 stop/fault 사건 |
| `/safety/physical_estop_active` | `std_msgs/Bool` | physical E-stop state |
| `/safety/heartbeat` | `shalom_interfaces/SafetyHeartbeat` | external watchdog heartbeat (required by default) |
| `/safety/command` | `shalom_interfaces/SafetyCommand` | operator E-stop/release/resume/fault clear |

| Output | Type |
|---|---|
| `/safety/state` | `shalom_interfaces/SafetyState` (motion permit 포함) |

heartbeat 감시는 기본 활성화되며, `alive=false`이거나 750 ms 동안 유효한 heartbeat가
오지 않으면 `controlled_stop`으로 전환한다. E-stop과 fault는 명시적 절차 없이는
motion을 재개하지 않는다.
