# Base source manager

`base_source_manager_node` is the only admitted autonomous publisher to
`/motion/autonomy/cmd_vel`. It receives raw NAV, DOCK, STAIR, and MISSION
`Twist` commands on `/motion/{nav,dock,stair,mission}/cmd_vel`. Selection uses
`/motion/base_source/select` (`shalom_interfaces/SelectBaseSource`), and
`/motion/base_source/state` is published at 20 Hz.

The request must include a nonempty, unique `request_id` and `requester`.
Selection is exclusive to the requester while the source is pending or active.
Repeating the same source by the same requester is idempotent. Only the owner
can release it with `NONE`; `NONE` is accepted from any requester when no
source is owned. `accepted=true` means the transition began, not that the
source is already active. Callers must wait for `ACTIVE` with their
`owner_requester` before starting a new action. Service rejections carry a
typed `result_code` (`NOT_READY`, `BUSY`, `INVALID`, `REPLAY`, or `FAILED`);
the human-readable `detail` is not a control condition.

Every source change immediately clears the previously active source, publishes an inhibit and
zero velocity, requests cancellation of ready `NavigateToPose` and `DockRobot`
action servers, and waits for all cancel responses plus a newer, fresh
`/motion/stopped` BASE stop. A 2 s transition timeout leaves the manager in
`FAULT`. After activation the selected command must arrive within 2 s and
continue at least every 300 ms; otherwise the manager faults and retains zero
without selecting another source. A new selection is required to recover.

Safety NORMAL with motion permitted, BASE_ACTIVE authority, and a fresh false
`/motion/manual_autonomy_lock` are required for autonomous selection and
output. The manager revokes pending and active ownership on an unsafe or stale
safety/authority state or a true or stale manual lock. Its
`/motion/base_source/inhibit` heartbeat is true in startup, transition, and
fault. Revocation or fault also requests best-effort cancellation of stale
Nav2/DockRobot goals; the next grant still requires strict cancellation and
BASE stop confirmation. It is false during active autonomous control only while
all guards remain fresh. For approved manual control it is also false while the manager
is INACTIVE/NONE and both `/motion/manual_autonomy_lock=true` and
`/motion/manual_ready=true` are fresh. Safety and authority guards still apply
to the manual release; the HMI's separate `/motion/base/inhibit` continues to
guard manual transitions.
