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
at `/motion/safe/cmd_vel`, deliberately **not** `/cmd_vel`. FR3 is also
intentionally blocked until its vendor stop/mode interface is integrated.

## 지금 주행 경로에 들어가 있는 것

`motion_mux` 하나다. `navigation.launch.py`가 `output_topic:=/cmd_vel`로
띄우고, Nav2는 `/motion/nav/cmd_vel`, hmi_bridge는 `/motion/teleop/cmd_vel`로
내보낸다. 둘이 `/cmd_vel`에 같이 쓰던 것을 중재 한 곳으로 모은 것이다. 덕분에
자율 주행 중 조작자 개입이 모드 전환 없이 되고(teleop이 fresh한 300 ms 동안만
이긴다), 수동 모드에서는 브릿지가 제자리 명령을 계속 내보내 자율 출력이
로봇까지 가지 않는다.

`safety_manager`·`motion_interlock_manager`·`safety_gate`·`teleop_bridge`는
아직 기동하지 않는다. `motion_mux`는 우선순위만 고르고 안전 판단은 하지
않으므로, gate를 B2 driver 앞에 넣는 일은 여전히 별도 검증 뒤에 할 일이다.
`teleop_bridge`는 조종기(UDP) 입력이 붙을 때 쓴다 — 관제 HMI는 자체 deadman을
가지고 있어 mux의 teleop 입력으로 바로 들어간다.
