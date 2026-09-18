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

`motion_mux`와 `safety_gate`까지가 실기 base command 경로이고, gate가
`/cmd_vel`의 유일한 발행자다. base source는 다음 우선순위로 들어온다.

| 우선순위 | source | topic | 누가 |
|---|---|---|---|
| 100 | teleop | `/motion/base/cmd_vel/teleop` | teleop_bridge (HMI UDP) |
| 90 | manual_hold | `/motion/base/cmd_vel/manual_hold` | hmi_bridge, 수동 모드 동안 0 |
| 40 | stair | `/motion/base/cmd_vel/stair` | (미연결) |
| 30 | dock | `/motion/base/cmd_vel/dock` | (미연결) |
| 20 | nav | `/motion/base/cmd_vel/nav` | Nav2 collision_monitor |

각 입력 lease는 300 ms다. `manual_hold`가 자율 source보다 위에 있는 이유는
수동 전환이 "로봇이 스스로 가는 것을 멈춘다"를 뜻해야 하기 때문이다. 이것이
없으면 Nav2가 계속 nav source를 채워, 조작자가 넘겨받은 줄 아는 동안에도
로봇이 목표를 향해 간다.

E-Stop은 UDP가 아니라 HMI TCP bridge에서 `/safety/software_estop_active`로
들어간다. 물리 E-Stop은 별도 `/safety/physical_estop_active` 입력이다. 둘 중
하나라도 활성화되면 `safety_manager`가 `e_stop_latched`로 내려가고, gate는 base
출력 **발행 자체를 멈춘다**. 0도 명령이고 비상정지는 명령하지 않는 것이 맞다 —
로봇은 driver의 300 ms 명령 시간초과로 선다. `controlled_stop`과 `fault`는
0을 계속 내보내 로봇을 세워 두고, 안전 관리자가 조용해지면 gate는 차단 쪽으로
닫힌다.

아직 다이어그램과 다른 곳: Nav2의 `docking_server`가 `/cmd_vel`에 직접 쓴다
(토픽 파라미터가 없다). 도킹 액션을 부르기 전에는 조용하지만, 자동 충전 복귀를
붙일 때 dock source로 돌려야 한다. watchdog(comm/health/process)도 아직 없다.

## 현장 UDP 설정

TCP와 UDP는 모두 기본 포트 번호 `9090`을 쓴다. 전송 계층이 달라 충돌하지 않는다.
`teleop_allowed_peer`에는 승인된 HMI PC의 고정 IP를 지정해야 하며, 비어 있으면
UDP 속도 명령을 fail-closed로 전부 버린다. 시뮬레이터는 `127.0.0.1`을 자동으로
설정한다.
