# mission_manager

과업지시서의 기본 흐름을 담은 C++ 미션 코어다. 목표 아키텍처용
`mission_manager::core::StateMachine`이 상위 상태를 소유하고, 실제 행동은 아래의
독립 BT가 맡는다.

```text
IDLE ─구성→ READY ─시작→ RUNNING ─점검 완료→ RETURNING(선택) → COMPLETED
                          │
                          └ 정지 요청 → PAUSING ─정지 확인→ PAUSED/IDLE/FAILED
                                                     └ 명시적 재개→ RECOVERING
```

| BT | 책임 | 실제 어댑터 |
|---|---|---|
| `Nav2Bt` | 점검 지점·충전 도크 이동 및 취소 | Nav2 action client |
| `CaptureBt` | AprilTag 보정 → MoveIt2 자세 → 정지 확인 → 촬영·저장 | Tag, MoveIt2, 카메라/NAS |
| `StairBt` | 계단 경로 안전 확인 → 전용 보행 모드 → 통과 → 출구 확인 | B2 계단 보행 controller |

모든 비동기 작업은 완료될 때까지 `Status::Running`을 반환한다. FSM이
`PAUSING`, `PAUSED`, `FAILED`이면 어떤 BT도 tick하지 않고, 실행 중인 BT에는
`halt()`를 호출해 Nav2·팔·계단 보행을 취소한다.

## 범위

- 목표 FSM: `IDLE`, `READY`, `RUNNING`, `PAUSING`, `PAUSED`, `RECOVERING`,
  `RETURNING`, `COMPLETED`, `FAILED`와 명시적 재개를 모델링한다.
- BT: 지점 작업과 복귀 순서를 모델링한다.
- `mission_manager_node`가 이 단일 `StateMachine`, immutable plan과 실행 adapter를
  소유한다. `hmi_bridge`는 typed service를 호출하고 상태를 HMI TCP로 변환할 뿐이다.
- waypoint operation은 `OperationRegistry`에 등록된 executor로 실행한다. 새 작업은
  executor와 필요 capability를 등록하며, 공통 plan 검증과 tick 경로는 바꾸지 않는다.
- `available_capabilities`는 코드 기본값이 아니라 로봇 배포 설정에서 명시한다.
  executor 등록과 capability 활성화가 모두 충족돼야 해당 작업을 수락한다.
- operation의 기본 capability는 executor가 소유한다. Mission plan의
  `required_capabilities`는 특정 미션이나 waypoint의 추가 요구사항에만 사용한다.
- 실제 E-stop, watchdog, `/cmd_vel` 최종 차단은 별도 `safety_manager`,
  `safety_gate`, 드라이버 레벨 timeout이 맡는다. 이 FSM만으로 안전 기능이 구현되지는 않는다.

## 지금까지 연결된 것

- `mission_manager::core::StateMachine`과 전이 테스트가 구현됐다. 이 코어는
  `PAUSING`에서 BT halt, gate zero, B2 정지가 모두 확인된 뒤에만 다음 상태로 간다.
- `Nav2Bt`가 점검포인트 순회와 도크 복귀를 모두 맡는다. `mission_manager_node`가
  `Nav2Runtime`을 구현해 Nav2 목표를 보내고 결과를 되돌려 받는다.
- 현재 배포 설정은 `navigation` capability와 `NAVIGATE_ONLY` executor만 활성화한다.
  연결되지 않은 작업은 plan 구성 단계에서 fail-closed로 거절한다.
- Base authority snapshot이 500 ms 이상 갱신되지 않거나 `BASE_ACTIVE`를 벗어나면
  실행 중인 Mission을 `PAUSING`으로, 복구 중인 Mission을 `PAUSED`로 전환한다.
- 상태는 `/mission/state`로 관제에 그대로 나간다.

## 아직 연결되지 않은 것

- `CaptureBt` — 순회 중 자동 촬영. AprilTag 보정과 MoveIt2 자세가 아직 없어서
  붙이지 않았다. 지금 촬영은 조작자가 누르는 수동 경로만 있다.
- `StairBt` — B2 계단 보행 모드가 없다.
- B2 시뮬레이션에서 HMI 연결 두절, pause/manual takeover, 명시적 resume를 함께
  검증하는 system test.

Nav2/HMI command source에서 `twist_mux → safety_gate → /cmd_vel`로 이어지는 경로와
별도 `safety_manager` 프로세스는 `robot_bringup/control.launch.py`에 연결돼 있다.

## 확인

ROS 환경에서는 다음으로 코어와 테스트를 빌드할 수 있다.

```bash
colcon build --packages-select mission_manager
colcon test --packages-select mission_manager
```
