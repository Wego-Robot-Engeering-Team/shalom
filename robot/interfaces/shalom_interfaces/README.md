# shalom_interfaces

Mission, operational safety, and base/arm authority boundaries use these typed
ROS 2 interfaces.  Wire values are part of the product contract; renumbering a
state or operation is a breaking change.

## Canonical endpoints

| Endpoint | Type | Intended QoS / semantics |
|---|---|---|
| `/mission/configure` | `ConfigureMission` service | immutable plan snapshot, idempotent request |
| `/mission/control` | `MissionControl` service | request ID based idempotency |
| `/mission/state` | `MissionState` topic | reliable, transient local, depth 1 |
| `/safety/command` | `SafetyCommand` service | explicit operator command |
| `/safety/event` | `SafetyEvent` topic | typed internal stop/fault event |
| `/safety/state` | `SafetyState` topic | reliable, transient local, depth 1 |
| `/safety/heartbeat` | `SafetyHeartbeat` topic | reliable; freshness uses receiver steady time |
| `/motion/authority/request` | `AuthorityRequest` service | request ID based idempotency |
| `/motion/authority` | `MotionAuthority` topic | reliable, transient local, depth 1 |
| `/motion/stopped` | `MotionStopped` topic | reliable; stale feedback is rejected |

`reason_code` is a stable identifier from
[`diagnostic_code_reference.md`](../../../docs/diagnostic_code_reference.md).
`detail` is only for logs and operator display and must never be parsed by control
logic.

The timestamps aid correlation.  Watchdog, command lease, permit lease, and stop
feedback freshness are always measured from local arrival time with a steady
clock.

Services require a non-empty `request_id`. A server caches a bounded set of
recent request IDs and returns the original result for a retry; it must not run
the transition action twice.

## Mission plan ownership

`hmi_bridge` reads the selected map bundle and sends one `MissionPlan` through
`/mission/configure`. Once accepted, `mission_manager` owns an immutable copy of
that plan. A bridge disconnect or restart therefore cannot erase or silently
change an active mission.

The plan is replaced only while Mission is `IDLE`, `READY`, `COMPLETED`, or
`FAILED`. `RUNNING`, `PAUSING`, `PAUSED`, `RECOVERING`, and `RETURNING` reject a
different plan revision. Retrying the same `request_id` returns the original
response without replacing the plan twice.

`NAVIGATE_ONLY` is the B2 simulation operation. `INSPECT` additionally requires
the capture capabilities declared by the waypoint. A missing capability rejects
the complete plan before Mission enters `READY`; a step is never faked or skipped.

When `return_to_dock` is true, `has_dock_approach` must also be true and the dock
waypoint must require only available capabilities. Reaching it means only that
the robot reached the approach pose.

The governing states and transitions are in the
[control architecture contract](../../../docs/control_architecture_contract.md).
