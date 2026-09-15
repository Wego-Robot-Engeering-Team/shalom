# Supervisory control plane

이 디렉터리의 `control`은 motor PID나 `ros2_control`을 뜻하지 않는다. 미션,
운용 권한, software safety를 조정하는 supervisory control plane이다. 실제 B2·FR3
저수준 드라이버는 `third_party/`에 있다.

```text
mission_manager ── action/intent ─────────────────────┐
teleop_bridge ────────────────────────────────┐        │
Nav2 / dock / stair ──────────────────────────┼─ motion_mux ─ safety_gate ─ driver
                                                │                         ↑
motion_interlock_manager ─ authority ──────────┘                  safety_manager
```

| Package | Owns | Does not own |
|---|---|---|
| `mission_manager` | Mission FSM and BT ordering | final actuator commands |
| `teleop_bridge` | deadman and input lease | hardware command topic |
| `motion_mux` | fresh base command source priority | safety state |
| `motion_interlock_manager` | base/arm operational authority | E-stop or fault state |
| `safety_manager` | software safety state and motion permit | physical E-stop circuit |
| `safety_gate` | final ROS command permission | physical safe stop |

`robot_bringup/control.launch.py` starts this plane with the final base output
at `/motion/safe/cmd_vel`, deliberately **not** `/cmd_vel`. Existing Nav2 and
HMI command producers still use legacy direct topics; they must be remapped to
`/motion/*/cmd_vel` and verified together before the gate is connected to the
B2 driver. FR3 is also intentionally blocked until its vendor stop/mode
interface is integrated.
