# Supervisory control plane

이 디렉터리의 `control`은 motor PID나 `ros2_control`을 뜻하지 않는다. 미션,
운용 권한, software safety를 조정하는 supervisory control plane이다. 실제 B2·FR3
저수준 드라이버는 `third_party/`에 있다.

목표 FSM, 이벤트 우선순위, 1초 통신 단절 정지와 B2-only 검증 범위는
[제어 아키텍처 계약](../../docs/control_architecture_contract.md)을 따른다. 아래 설명은
현재 구현 상태이며 목표 계약과 다른 부분은 계약의 구현 순서에 따라 변경한다.

```text
mission_manager ── action/intent ─────────────────────┐
teleop_bridge ────────────────────────────────┐        │
Nav2 / dock / stair ──────────────────────────┼─ twist_mux ─ safety_gate ─ driver
                                                │                         ↑
motion_interlock_manager ─ authority ──────────┘                  safety_manager
```

| Package | Owns | Does not own |
|---|---|---|
| `mission_manager` | Mission FSM and BT ordering | final actuator commands |
| `teleop_bridge` | deadman and input lease | hardware command topic |
| `twist_mux` | fresh base command source priority | safety state |
| `motion_interlock_manager` | base/arm operational authority and stopped feedback validation | E-stop or fault state |
| `safety_manager` | software safety state and motion permit | physical E-stop circuit |
| `safety_gate` | final ROS command permission | physical safe stop |

`robot_bringup/control.launch.py` connects `twist_mux → safety_gate` and accepts
the final driver topic as `base_output_topic`. Physical and B2 simulation
bringup both set it to `/cmd_vel`, so Safety Gate is the only publisher on the
driver command topic. FR3 is intentionally blocked until its vendor stop/mode
interface is integrated.

## 현재 주행 경로

Nav2는 `/motion/nav/cmd_vel`, hmi_bridge는 `/motion/teleop/cmd_vel`로 내보낸다.
표준 ROS 2 `twist_mux`가 둘을 포함한 모든 source를 중재하고, Safety Gate만
`/cmd_vel`로 내보낸다. 덕분에
자율 주행 중 조작자 개입이 모드 전환 없이 되고(teleop이 fresh한 300 ms 동안만
이긴다), 수동 모드에서는 브릿지가 제자리 명령을 계속 내보내 자율 출력이
로봇까지 가지 않는다.

`safety_manager`·`motion_interlock_manager`·`safety_gate`·`teleop_bridge`도
`control.launch.py`에서 함께 기동한다. Mission과 Safety 런타임은 각 패키지의
단일 목표 FSM을 사용한다. Mission Manager는 독립 프로세스로 실행되며 Mission,
Safety, Motion Authority 경계는 `shalom_interfaces`의 typed topic/service를 사용한다.
`base_motion_monitor`는 최종 명령과 B2 odometry를 결합해 `/motion/stopped`를
발행하며, Mission pause 완료와 base/arm authority 전환은 이 피드백을 사용한다.
시뮬레이션 bringup은 `/b2/odom_gt`와 simulation time을, 실물 bringup은
`/b2/odom`과 system time을 선택한다. 신선도 timeout은 steady clock으로 판정한다.
`twist_mux`는 우선순위만 고르고 안전 판단은 하지 않는다.
`teleop_bridge`는 조종기(UDP) 입력이 붙을 때 쓴다 — 관제 HMI는 자체 deadman을
가지고 있어 mux의 teleop 입력으로 바로 들어간다.
