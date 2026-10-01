# Supervisory control plane

이 디렉터리의 `control`은 motor PID나 `ros2_control`을 뜻하지 않는다. 미션,
운용 권한, software safety를 조정하는 supervisory control plane이다. 실제 B2·FR3
저수준 드라이버는 `third_party/`에 있다.

목표 FSM, 이벤트 우선순위, 1초 통신 단절 정지와 B2-only 검증 범위는
[제어 아키텍처 계약](../../docs/control_architecture_contract.md)을 따른다. 아래 설명은
현재 구현 상태이며 목표 계약과 다른 부분은 계약의 구현 순서에 따라 변경한다.

```text
Nav2 / mission / dock / stair ─ base_source_manager ─┐
HMI UDP ─ teleop_bridge ──────────────────────────────┼─ twist_mux ─ safety_gate ─ B2 driver
HMI TCP ─ manual_hold / manual_autonomy_lock ─────────┘                  ↑
                                        safety_manager / motion_interlock_manager

HMI arm / FR3 BT ─ joint_mux ─ safety_gate ─ FR3 driver
```

| Package | Owns | Does not own |
|---|---|---|
| `mission_manager` | Mission FSM and BT ordering | final actuator commands |
| `teleop_bridge` | deadman and input lease | hardware command topic |
| `base_source_manager` | 하나의 자율 base source 소유권과 전환 시 정지 확인 | HMI 수동 모드 정책 |
| `twist_mux` | 승인된 자율 입력과 teleop의 priority | 자율 source 간 전환, safety state |
| `joint_mux` | fresh arm command source priority | FR3 stop/mode control |
| `motion_interlock_manager` | base/arm operational authority and stopped feedback validation | E-stop or fault state |
| `safety_manager` | software safety state and motion permit | physical E-stop circuit |
| `safety_gate` | final ROS command permission | physical safe stop |

`robot_bringup/control.launch.py` connects `base_source_manager → twist_mux → safety_gate` and accepts
the final driver topic as `base_output_topic`. Physical and B2 simulation
bringup both set it to `/cmd_vel`, so Safety Gate is the only publisher on the
driver command topic. FR3 is intentionally blocked until its vendor stop/mode
interface is integrated.

## 현재 주행 경로

`base_source_manager`는 NAV/DOCK/STAIR/MISSION 중 명시적으로 선택된 자율 입력만
`/motion/autonomy/cmd_vel`로 전달한다. `twist_mux`는 이 단일 자율 입력과 수동 입력을
중재하며, `safety_gate`만 `/cmd_vel`을 발행한다. mux 입력 lease는 300 ms이다.

| 우선순위 | source | topic | 누가 |
|---|---|---|---|
| 100 | teleop | `/motion/teleop/cmd_vel` | teleop_bridge (HMI UDP) |
| 90 | manual_hold | `/motion/manual_hold/cmd_vel` | hmi_bridge, 수동 모드 동안 0 |
| 80 | autonomy | `/motion/autonomy/cmd_vel` | base_source_manager가 선택한 단일 source |

자율 source 전환 요청은 `/motion/base_source/select`로 한다. 관리자는 먼저 별도의
`/motion/base_source/inhibit=true`로 최종 출력을 차단하고 이전 Nav2/DockRobot
action 취소 응답 및 취소 이후의 신선한 BASE 정지 피드백을 기다린다. 안전 상태와
BASE 권한이 유효할 때만 새 source를 `ACTIVE`로 만든다. 취소/정지 확인 실패나
선택된 source의 명령 유실은 `FAULT` 및 0 출력으로 처리한다. 이때 다른 자율
source로 자동 강등하지 않는다. Mission은 START/RESUME 시도마다 새 requester를
사용하므로 이전 시도의 `ACTIVE` 상태를 새 실행 허가로 오인하지 않는다.
HMI의 전환 차단 `/motion/base/inhibit`와
source 관리자의 차단은 `safety_gate`에서 각각 검사하며, 둘 중 하나라도
유효하지 않거나 true이면 통과시키지 않는다. 선택 상태는
`/motion/base_source/state`에서 확인한다. 현재 미션의 `dock_approach`는
여전히 NavigateToPose 접근 단계이며, 물리 DockRobot action의 source 선택은
이 인터페이스를 사용할 별도 실행기에서 맡아야 한다.

수동 전환 요청 직후 HMI bridge가 `/motion/base/inhibit=true`를 보내고,
`safety_gate`는 전환 완료까지 20 Hz로 0을 출력해 teleop까지 차단한다.
이 신호가 250 ms 동안 끊겨도 gate는 0을 유지하고, 전환 해제 시 이전 명령을
재사용하지 않는다. 자동 복귀 후에는 이전 teleop의 300 ms mux lease가
만료되도록 700 ms 동안 차단을 유지한다.
HMI bridge 재시작 시에도 초기 lock/inhibit를 유지하고, source 관리자가
이전 소유권을 비운 `INACTIVE` 상태를 새로 발행한 뒤에만 차단을 해제한다.
`manual_autonomy_lock`(우선순위 90, Bool, 300 ms lease)이 낮은 자율 source를
차단한다. Lock publisher가 죽어 lease가 만료되어도 잠긴 상태로 처리한다.
`manual_hold` 0은 lock만으로는 새 0 명령을 발행하지 않는 특성을 보완한다.
진행 중인 Nav2 goal 취소, Mission PAUSED(또는 READY의 pending START 취소),
신선한 `/motion/stopped` BASE 정지 피드백, Safety NORMAL·BASE 권한을 확인한
뒤에도 기존 mux lock lease를 비우기 위해 최소 350 ms가 지난 후에만
`/motion/manual_ready=true`를 발행한다. `teleop_bridge`는 이 신호가
150 ms 안에 갱신되지 않으면 명령을 차단한다. 확인 실패 시 2초 내 HMI 요청을
실패로 돌려주고 정지·lock은 유지한다. 자동 복귀는 Mission을 재개하지 않는다.
HMI에는 확인 전 또는 허가 철회 시 `state/safety.mode=transitioning`을 보고해
확정된 수동 모드로 표시하지 않는다.

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
