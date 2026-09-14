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
- 아직 ROS 노드나 실제 명령 송신은 넣지 않았다. 현재 `hmi_bridge`가 Nav2·촬영
  명령을 직접 소유하므로, 지금 함께 실행하면 명령 소유자가 둘이 된다.
- 실제 E-stop, 3초 통신 watchdog, `/cmd_vel` 최종 차단은 별도
  `safety_manager`와 드라이버 레벨 타임아웃이 맡아야 한다. 이 FSM만으로 안전 기능이
  구현되지는 않는다.

## 연결 순서

1. `hmi_bridge`의 미션 실행 책임을 이 패키지로 옮긴다.
2. 각 BT runtime의 ROS 2 어댑터에서 Nav2 goal/cancel, 촬영 완료 응답, 계단 controller, 도크 goal을 구현한다.
3. `safety_manager`가 안전 전이를 전달하고 모든 주행 명령의 최종 gate가 된다.
4. 그 뒤에만 `inspection.launch.py`에서 mission_manager와 safety_manager를 함께 기동한다.

## 확인

ROS 환경에서는 다음으로 코어와 테스트를 빌드할 수 있다.

```bash
colcon build --packages-select mission_manager
colcon test --packages-select mission_manager
ros2 run mission_manager mission_manager_demo
```
