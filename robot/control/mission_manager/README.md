# mission_manager

과업지시서의 기본 흐름을 담은 C++ 미션 코어 예제다. `MissionFsm`이 상위 상태를
소유하고, 실제 행동은 아래의 독립 BT가 맡는다.

```text
대기 ─시작→ 작업 중 ─모든 지점 완료/저전력→ 복귀 → 완료
              │ 이동 → 정지 확인 → 촬영 → 다음 지점
              ├ 통신 단절·수동 전환·일시정지 → 일시정지
              └ 실패 → Fault

어느 상태에서나 E-stop → EmergencyStopped → (해제) → 일시정지
```

| BT | 책임 | 실제 어댑터 |
|---|---|---|
| `Nav2Bt` | 점검 지점·충전 도크 이동 및 취소 | Nav2 action client |
| `CaptureBt` | AprilTag 보정 → MoveIt2 자세 → 정지 확인 → 촬영·저장 | Tag, MoveIt2, 카메라/NAS |
| `StairBt` | 계단 경로 안전 확인 → 전용 보행 모드 → 통과 → 출구 확인 | B2 계단 보행 controller |

모든 비동기 작업은 완료될 때까지 `kRunning`을 반환한다. FSM이 `Paused`, `Fault`,
`EmergencyStopped`이면 어떤 BT도 tick하지 말고, 실행 중인 BT에는 `halt()`를 호출해
Nav2·팔·계단 보행을 취소한다.

## 범위

- FSM: 수동 우선, 링크 단절, 저전력 복귀, E-stop 해제 후 명시적 재개를 모델링한다.
- BT: 지점 작업과 복귀 순서를 모델링한다.
- ROS 노드는 아직 없다. `hmi_bridge`가 이 라이브러리를 링크해 `Nav2Runtime`을
  구현하고, 미션 tick을 돌린다. 어댑터가 IO를 갖고 이 패키지가 순서를 갖는
  구조이므로, 같은 개념이 두 곳에 있지 않다.
- 실제 E-stop, 3초 통신 watchdog, `/cmd_vel` 최종 차단은 별도
  `safety_manager`와 드라이버 레벨 타임아웃이 맡아야 한다. 이 FSM만으로 안전 기능이
  구현되지는 않는다.

## 지금까지 연결된 것

- `MissionFsm`이 미션 상태를 소유한다. `hmi_bridge`의 3단계 상태기계는 없앴다.
- `Nav2Bt`가 점검포인트 순회와 도크 복귀를 모두 맡는다. `hmi_bridge`가
  `Nav2Runtime`을 구현해 Nav2 목표를 보내고 결과를 되돌려 준다.
- 상태는 `state/mission`으로 관제에 그대로 나간다(`returning`·`completed`·
  `fault`·`emergency_stopped` 포함).

## 아직 연결되지 않은 것

- `CaptureBt` — 순회 중 자동 촬영. AprilTag 보정과 MoveIt2 자세가 아직 없어서
  붙이지 않았다. 지금 촬영은 조작자가 누르는 수동 경로만 있다.
- `StairBt` — B2 계단 보행 모드가 없다.
- `safety_manager` — 3초 통신 watchdog과 `/cmd_vel` 최종 차단은 여전히
  `hmi_bridge`와 드라이버 레벨이 맡는다.

## 확인

ROS 환경에서는 다음으로 코어와 테스트를 빌드할 수 있다.

```bash
colcon build --packages-select mission_manager
colcon test --packages-select mission_manager
ros2 run mission_manager mission_manager_demo
```
