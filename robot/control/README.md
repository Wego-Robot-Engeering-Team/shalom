# Supervisory control plane

이 디렉터리의 `control`은 motor PID나 `ros2_control`을 뜻하지 않는다. 미션,
운용 권한, software safety를 조정하는 supervisory control plane이다. 실제 B2·FR3
저수준 드라이버는 `third_party/`에 있다.

목표 FSM, 이벤트 우선순위, 1초 통신 단절 정지와 B2-only 검증 범위는
[제어 아키텍처 계약](../../docs/control_architecture_contract.md)을 따른다. 아래 설명은
현재 구현 상태이며 목표 계약과 다른 부분은 계약의 구현 순서에 따라 변경한다.

```text
mission_manager ── action/intent ─────────────────────┐
HMI UDP ─ teleop_bridge ───────────────────────┐        │
HMI TCP ─ manual_hold ─────────────────────────┤        │
Nav2 / mission / dock / stair ─────────────────┼─ twist_mux ─ safety_gate ─ B2 driver
                                                │                         ↑
motion_interlock_manager ─ authority ──────────┘                  safety_manager

HMI arm / FR3 BT ─ joint_mux ─ safety_gate ─ FR3 driver
```

| Package | Owns | Does not own |
|---|---|---|
| `mission_manager` | Mission FSM and BT ordering | final actuator commands |
| `teleop_bridge` | deadman and input lease | hardware command topic |
| `twist_mux` | fresh base command source priority | safety state |
| `joint_mux` | fresh arm command source priority | FR3 stop/mode control |
| `motion_interlock_manager` | base/arm operational authority and stopped feedback validation | E-stop or fault state |
| `safety_manager` | software safety state and motion permit | physical E-stop circuit |
| `safety_gate` | final ROS command permission | physical safe stop |

`robot_bringup/control.launch.py` connects `twist_mux → safety_gate` and accepts
the final driver topic as `base_output_topic`. Physical and B2 simulation
bringup both set it to `/cmd_vel`, so Safety Gate is the only publisher on the
driver command topic. FR3 is intentionally blocked until its vendor stop/mode
interface is integrated.

## 현재 주행 경로

표준 ROS 2 `twist_mux`와 `safety_gate`까지가 실기 base command 경로이며,
gate만 `/cmd_vel`을 발행한다. 각 입력의 lease는 300 ms이고 우선순위는 다음과 같다.

| 우선순위 | source | topic | 누가 |
|---|---|---|---|
| 100 | teleop | `/motion/teleop/cmd_vel` | teleop_bridge (HMI UDP) |
| 90 | manual_hold | `/motion/manual_hold/cmd_vel` | hmi_bridge, 수동 모드 동안 0 |
| 80 | mission | `/motion/mission/cmd_vel` | Mission 동작 source |
| 40 | stair | `/motion/stair/cmd_vel` | Stair BT |
| 30 | dock | `/motion/dock/cmd_vel` | Dock BT/Nav2 docking server |
| 20 | nav | `/motion/nav/cmd_vel` | Nav2 collision monitor |

`manual_hold`는 수동 전환 직후 조작 입력이 없어도 자율 source가 로봇에 도달하지
않게 한다. 동시에 Mission Manager에 manual takeover를 전달해 현재 BT를 안전하게
pause한다. teleop deadman 또는 lease가 끝나면 teleop source는 만료된다.

`safety_manager`·`motion_interlock_manager`·`safety_gate`·`teleop_bridge`도
`control.launch.py`에서 함께 기동한다. Mission과 Safety 런타임은 각 패키지의
단일 목표 FSM을 사용한다. Mission Manager는 독립 프로세스로 실행되며 Mission,
Safety, Motion Authority 경계는 `shalom_interfaces`의 typed topic/service를 사용한다.
`base_motion_monitor`는 최종 명령과 B2 odometry를 결합해 `/motion/stopped`를
발행하며, Mission pause 완료와 base/arm authority 전환은 이 피드백을 사용한다.
시뮬레이션 bringup은 `/b2/odom_gt`와 simulation time을, 실물 bringup은
`/b2/odom`과 system time을 선택한다. 신선도 timeout은 steady clock으로 판정한다.
`twist_mux`는 우선순위만 고르고 안전 판단은 하지 않는다.

Software E-Stop은 UDP가 아니라 HMI TCP bridge가 typed `/safety/command`
서비스로 전달한다. 물리 E-Stop도 Safety Manager의 별도 입력이다. 둘 중 하나라도
활성화되면 `safety_manager`가 `E_STOP_LATCHED`로 내려가고, gate는 base
출력 **발행 자체를 멈춘다**. 0도 명령이고 비상정지는 명령하지 않는 것이 맞다 —
로봇은 driver의 300 ms 명령 시간초과로 선다. `controlled_stop`과 `fault`는
0을 계속 내보내 로봇을 세워 두고, 안전 관리자가 조용해지면 gate는 차단 쪽으로
닫힌다.

## 현장 UDP 설정

TCP와 UDP는 모두 기본 포트 번호 `9090`을 쓴다. 전송 계층이 달라 충돌하지 않는다.
`teleop_allowed_peer`에는 승인된 HMI PC의 고정 IP를 지정해야 하며, 비어 있으면
UDP 속도 명령을 fail-closed로 전부 버린다. 시뮬레이터는 `127.0.0.1`을 자동으로
설정한다.
