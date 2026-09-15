# safety_manager

ROS 수준의 운용 안전 상태를 하나로 소유한다. 물리 E-stop 회로를 대체하지 않으며,
물리 E-stop 상태는 `/safety/physical_estop_active`로 이 노드에 **통지**돼야 한다.

| Input | Type | Meaning |
|---|---|---|
| `/safety/event` | `std_msgs/String` | `request_stop`, `health_fault`, `clear_fault`, `resume` |
| `/safety/physical_estop_active` | `std_msgs/Bool` | physical E-stop state |
| `/safety/heartbeat` | `std_msgs/Bool` | optional external watchdog heartbeat |

| Output | Type |
|---|---|
| `/safety/state` | `std_msgs/String`: `normal`, `controlled_stop`, `e_stop_latched`, `fault` |
| `/safety/motion_permitted` | `std_msgs/Bool` |

`require_external_heartbeat:=true`일 때 heartbeat가 `heartbeat_timeout_ms` 안에 오지
않으면 `controlled_stop`으로 전환한다. E-stop과 fault는 명시적 절차 없이는 motion을
재개하지 않는다.
