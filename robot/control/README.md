# Supervisory control plane

이 디렉터리의 `control`은 motor PID나 `ros2_control`을 뜻하지 않는다. 미션,
운용 권한, software safety를 조정하는 supervisory control plane이다. 실제 B2·FR3
저수준 드라이버는 `third_party/`에 있다.

```text
mission_manager ── action/intent ─────────────────────┐
HMI UDP ─ teleop_bridge ───────────────────────┐        │
Nav2 / dock / stair ──────────────────────────┼─ motion_mux ─ safety_gate ─ B2 driver
                                                │                         ↑
motion_interlock_manager ─ authority ──────────┘                  safety_manager

HMI arm / FR3 BT ─ joint_mux ─ safety_gate ─ FR3 driver
```

| Package | Owns | Does not own |
|---|---|---|
| `mission_manager` | Mission FSM and BT ordering | final actuator commands |
| `teleop_bridge` | deadman and input lease | hardware command topic |
| `motion_mux` | fresh base command source priority | safety state |
| `joint_mux` | fresh arm command source priority | FR3 stop/mode control |
| `motion_interlock_manager` | base/arm operational authority | E-stop or fault state |
| `safety_manager` | software safety state and motion permit | physical E-stop circuit |
| `safety_gate` | final ROS command permission | physical safe stop |

`robot_bringup/control.launch.py` starts this plane with the final base output
at `/motion/safe/cmd_vel`, deliberately **not** `/cmd_vel`. `joint_mux`도
같이 기동하지만, FR3 vendor stop/mode 연동 전까지 `safety_gate`의 팔 출력은
기본 비활성이다.

## 지금 주행 경로에 들어가 있는 것

`motion_mux`와 `safety_gate`까지가 실기 base command 경로다. Nav2는
`/motion/nav/cmd_vel`, HMI는 UDP `teleop_bridge`를 통해
`/motion/teleop/cmd_vel`로 들어간다. mux가 fresh한 300 ms lease 기준으로
`teleop > mission > stair > dock > nav` 우선순위를 고르고, gate가 안전 허가와
authority를 다시 확인한 뒤에만 B2 driver로 내보낸다.

E-Stop은 UDP가 아니라 HMI TCP bridge에서 `/safety/software_estop_active`로
들어간다. 물리 E-Stop은 별도 `/safety/physical_estop_active` 입력이다. 둘 중
하나라도 활성화되면 `safety_manager`가 motion permit을 내리고 gate가 base 출력을
0으로 만든다.

## 현장 UDP 설정

TCP와 UDP는 모두 기본 포트 번호 `9090`을 쓴다. 전송 계층이 달라 충돌하지 않는다.
`teleop_allowed_peer`에는 승인된 HMI PC의 고정 IP를 지정해야 하며, 비어 있으면
UDP 속도 명령을 fail-closed로 전부 버린다. 시뮬레이터는 `127.0.0.1`을 자동으로
설정한다.
