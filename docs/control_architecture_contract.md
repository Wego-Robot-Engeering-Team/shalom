# 제어 아키텍처 계약

이 문서는 Shalom의 Mission, Safety, Motion Authority 제어 경계를 정의한다.
구현 코드보다 이 문서가 먼저이며, 상태나 이벤트 의미를 변경할 때는 코드와 테스트를
고치기 전에 이 계약을 갱신한다.

현재 검증 대상은 **HMI + B2 MuJoCo 시뮬레이터**다. FR3, 계단 주행, 물리 도킹과
충전, 물리 E-Stop은 인터페이스만 예약하고 가짜 성공 경로를 만들지 않는다.

## 확정된 결정

- `mission_manager`는 `hmi_bridge`와 독립된 프로세스로 실행한다.
- 1차 HMI 통신은 현재 raw TCP 단일 연결을 유지한다.
- HMI 통신 단절 시 마지막 정상 heartbeat 수신부터 Safety Gate의 첫 zero output까지
  1,000 ms를 넘지 않는다.
- FSM은 우선 명시적인 C++ 상태·이벤트 구현을 유지한다. Boost.SML은 도입하지 않는다.
- Mission FSM은 `IDLE`, `READY`, `RUNNING`, `PAUSING`, `PAUSED`,
  `RECOVERING`, `RETURNING`, `COMPLETED`, `FAILED`를 사용한다.
- 수동 전환은 실행 중인 Nav2 goal을 취소하고 Mission을 `PAUSED`로 만든다.
- 재개할 때 완료된 waypoint는 유지하고, 중단된 waypoint는 처음부터 다시 실행한다.
- 도크 접근은 미션별 선택 사항이다. 접근 위치 도달을 물리 도킹이나 충전 성공으로
  간주하지 않는다.
- 지원하지 않는 기능은 capability 검사에서 거부한다. 시뮬레이션용 가짜 성공 BT를
  만들지 않는다.

## 책임과 데이터 흐름

```text
HMI ─TCP→ hmi_bridge ─request→ mission_manager ─intent→ Nav2 / motion source
                         │              │
                         │              └─ halt/cancel/result
                         │
                         └─ heartbeat/software E-Stop
                                      ↓
Watchdog / Health ─────────────→ safety_manager ─ motion_permitted ─┐
                                                                    │
mission_manager ─request→ motion_interlock_manager ─ authority ─────┼→ safety_gate → B2
                                                                    │
motion sources ───────────────────→ twist_mux ─ selected command ───┘
```

Safety Manager와 Motion Interlock Manager는 직렬 관계가 아니다. Safety Gate가
`motion_permitted`, authority, command freshness를 독립적으로 검사한다.

| 구성요소 | 소유하는 것 | 소유하지 않는 것 |
|---|---|---|
| `hmi_bridge` | TCP 세션, framing, 명령 변환, heartbeat | Mission 진행 상태, 안전 허가 |
| `mission_manager` | Mission FSM, Mission BT, 단계 checkpoint | 최종 actuator 명령, E-Stop latch |
| `safety_manager` | Safety FSM, motion permit | base/arm 선택, 물리 E-Stop 회로 |
| `motion_interlock_manager` | base/arm 배타적 authority | 안전 상태, Mission 진행 |
| `twist_mux` | fresh command source 선택 | 안전 판단, 운용 모드 |
| `safety_gate` | 최종 ROS 명령 통과 또는 차단 | Mission 정책, 물리 안전정지 |
| HMI | 요청과 상태 표시 | 로봇 상태의 원본 |

Safety Gate는 다음 조건이 모두 참일 때만 base command를 통과시킨다.

```text
fresh(motion_permitted == true)
AND authority == BASE_ACTIVE
AND fresh(selected_command)
AND driver_ready
```

하나라도 거짓이거나 알 수 없으면 zero command를 출력한다. Safety Gate만 B2 최종
command topic을 발행할 수 있어야 한다.

## 시간 계약

### HMI 통신 단절

정지 시간의 시작과 끝은 다음과 같다.

```text
T0: 로봇측 hmi_bridge가 마지막 정상 HMI heartbeat를 받은 시각
T1: Safety Gate가 첫 zero Twist를 발행한 시각
요구사항: T1 - T0 <= 1,000 ms
```

1차 구현의 목표 budget은 다음과 같다.

| 항목 | 목표값 | 의미 |
|---|---:|---|
| HMI heartbeat | 5 Hz | 200 ms 간격 |
| HMI link timeout | 600 ms | 3회 heartbeat 누락 |
| 외부 heartbeat timeout | 750 ms 이하 | bridge 프로세스 자체 소실 검출 |
| Safety permit timeout | 250 ms | safety manager 소실 시 gate 차단 |
| command source lease | 300 ms | 오래된 command 무효화 |
| B2 command timeout | 300 ms | driver 방어 계층 |
| Gate output | 20 Hz 이상 | 최대 50 ms 출력 지연 |

`/safety/heartbeat`의 의미는 다음과 같다.

- `true`: source가 살아 있고 HMI heartbeat도 유효하다.
- `false`: 즉시 `CONTROLLED_STOP` 사건으로 처리한다.
- 메시지 없음: external heartbeat timeout 뒤 `CONTROLLED_STOP`으로 처리한다.

메시지 도착 시각만 갱신하고 Bool 값을 무시해서는 안 된다.

진단 문서의 `LINK_LOST`, `LINK_HEARTBEAT_TIMEOUT`,
`SAFETY_COMM_TIMEOUT_STOP`도 이 1초 정지 계약을 사용한다.

모든 lease와 timeout은 ROS simulation time이 아니라 monotonic/steady clock으로
판정한다.

## Capability 계약

### B2 시뮬레이션에서 제공

- `base_motion`
- `nav2`
- `lidar`
- `imu`
- `b2_joint_state`
- `software_estop`
- `dock_approach`

### B2 시뮬레이션에서 제공하지 않음

- `arm_motion`
- `stair_motion`
- `physical_docking`
- `charging`
- `physical_estop`
- `capture`

Mission이 요구하는 capability가 없으면 `READY`에 진입하지 않는다. HMI에는 기능을
비활성 상태로 표시하고 원인 코드를 보낸다. 실행 중 성공으로 건너뛰지 않는다.

### Mission Plan 소유권

`hmi_bridge`는 선택된 지도 번들의 waypoint를 `MissionPlan` snapshot으로 만들어
`/mission/configure` service로 전달한다. 수락된 뒤에는 `mission_manager`가 immutable
plan과 checkpoint를 소유한다. 따라서 HMI 또는 bridge가 끊기거나 재시작돼도 실행
중인 plan이 사라지거나 바뀌지 않는다.

- plan은 Mission ID, map ID, revision, waypoint 배열과 선택적 dock approach를 가진다.
- `request_id` 재전송은 기존 응답을 반환하고 plan을 두 번 적용하지 않는다.
- active 상태에서는 다른 plan/revision으로 교체할 수 없다.
- `return_to_dock=true`이면 dock approach가 반드시 있어야 `READY`에 진입한다.
- B2-only waypoint는 `NAVIGATE_ONLY`를 사용한다.
- `INSPECT` waypoint는 필요한 capture capability가 없으면 plan 전체를 거부한다.

도킹은 다음 세 단계로 분리한다.

```text
NavigateToDockApproach  # 현재 검증 범위
AlignAndEngageDock      # 실물 도착 후
ConfirmCharging         # 실물 도착 후
```

## Mission FSM

### 상태

| 상태 | 의미 |
|---|---|
| `IDLE` | 선택되거나 준비된 Mission이 없음 |
| `READY` | Mission 데이터와 capability 검증 완료. 이동 허가는 별도 검사 |
| `RUNNING` | 점검 단계 실행 중 |
| `PAUSING` | BT halt, action cancel, zero output과 B2 정지를 기다리는 중 |
| `PAUSED` | 모든 active leaf가 멈추고 B2 정지가 확인됨 |
| `RECOVERING` | 명시적 resume 뒤 health, localization, safety, authority 재검증 중 |
| `RETURNING` | 선택적 도크 접근 위치로 복귀 중 |
| `COMPLETED` | 정상 완료 |
| `FAILED` | 명시적 reset이 필요한 Mission 실패 |

Mission FSM은 E-Stop 상태를 중복 소유하지 않는다. 예를 들어 E-Stop 중 시스템 상태는
`Mission=PAUSED`, `Safety=E_STOP_LATCHED`로 표시한다.

### 이벤트 우선순위

같은 event cycle에 여러 사건이 들어오면 다음 순서로 처리한다.

```text
physical E-Stop
> software E-Stop
> critical safety fault
> watchdog/link timeout
> operator stop
> pause/manual takeover
> BT failure
> BT success
> start/resume
```

모든 외부 callback은 하나의 Mission event queue에 넣고 직렬로 처리한다. callback이
FSM을 직접 동시에 호출하지 않는다.

### 수락 전이

아래 표에 없는 전이는 거부하고 현재 상태를 유지한다. 같은 pause/stop 사건의 재전송은
부작용 없이 성공으로 응답하는 idempotent 명령으로 처리한다.

`PAUSING`은 pause뿐 아니라 stop과 failure의 공통 정지 구간이다. 진입할 때
`halt_target`을 `PAUSED`, `IDLE`, `FAILED` 중 하나로 저장한다. `resume_target`은
중단 전 실행 단계인 `RUNNING` 또는 `RETURNING`이다.

| 현재 상태 | 사건 | Guard/Context | 다음 상태 | 핵심 action |
|---|---|---|---|---|
| `IDLE` | `MISSION_CONFIGURED` | 데이터와 capability 유효 | `READY` | checkpoint 초기화 |
| `READY` | `START_REQUESTED` | Safety Normal, health/localization 정상, base authority 획득 | `RUNNING` | Mission BT 시작 |
| `READY` | `STOP_REQUESTED` | — | `IDLE` | Mission 해제 |
| `RUNNING` | `PAUSE_REQUESTED` | — | `PAUSING` | target=`PAUSED`, BT halt |
| `RUNNING` | `MANUAL_TAKEOVER` | — | `PAUSING` | target=`PAUSED`, Nav2 cancel |
| `RUNNING` | `LINK_LOST`/`SAFETY_STOP` | — | `PAUSING` | target=`PAUSED`, BT halt |
| `RETURNING` | `PAUSE_REQUESTED`/`MANUAL_TAKEOVER` | — | `PAUSING` | target=`PAUSED`, resume=`RETURNING` |
| `RETURNING` | `LINK_LOST`/`SAFETY_STOP` | — | `PAUSING` | target=`PAUSED`, resume=`RETURNING` |
| `RUNNING`/`RETURNING` | `STOP_REQUESTED` | — | `PAUSING` | target=`IDLE`, BT halt |
| `RUNNING`/`RETURNING` | `FATAL_STEP_FAILURE` | — | `PAUSING` | target=`FAILED`, 원인 latch |
| `PAUSING` | `MOTION_QUIESCED` | BT halted, gate zero, B2 stopped | `halt_target` | 정지 완료 기록 |
| `PAUSED` | `RESUME_REQUESTED` | 자동 재개 금지; 운영자 명시 요청 | `RECOVERING` | health/authority 재검증 시작 |
| `PAUSED` | `STOP_REQUESTED` | — | `IDLE` | Mission과 checkpoint 해제 |
| `RECOVERING` | `RECOVERY_READY` | Safety Normal, health/localization 정상, base authority 획득 | `resume_target` | 중단 waypoint 재요청 |
| `RECOVERING` | `SAFETY_STOP` | — | `PAUSED` | recovery 취소 |
| `RECOVERING` | `STOP_REQUESTED` | — | `IDLE` | Mission 해제 |
| `RUNNING` | `INSPECTION_COMPLETE` | `return_to_dock=true` | `RETURNING` | dock approach leaf 시작 |
| `RUNNING` | `INSPECTION_COMPLETE` | `return_to_dock=false` | `COMPLETED` | 완료 기록 |
| `RETURNING` | `RETURN_COMPLETE` | dock approach 도달 | `COMPLETED` | 복귀 완료 기록 |
| `COMPLETED`/`FAILED` | `RESET_REQUESTED` | — | `IDLE` | terminal 상태 해제 |

`PAUSING → PAUSED`는 Nav2 cancel 응답만으로 완료되지 않는다. 다음 조건이 모두
참이어야 `MOTION_QUIESCED`를 발생시킨다.

- active BT leaf가 halt 완료됨
- Safety Gate가 zero output 상태임
- B2 정지 피드백이 신선하고 정지 기준을 만족함

Resume은 마지막으로 완료한 waypoint 다음이 아니라 **중단됐던 현재 waypoint를
처음부터 다시 요청**한다. 완료된 waypoint checkpoint는 유지한다.

## Safety FSM

### 상태와 permit

| 상태 | motion permit |
|---|---|
| `INITIALIZING` | false |
| `CONTROLLED_STOP` | false |
| `NORMAL` | true |
| `E_STOP_LATCHED` | false |
| `FAULT` | false |

Safety Manager 재시작 상태는 항상 `INITIALIZING`이다.

### 수락 전이

| 현재 상태 | 사건 | Guard | 다음 상태 |
|---|---|---|---|
| `INITIALIZING` | `INPUTS_READY` | 필수 입력이 모두 신선하고 E-Stop 해제 | `CONTROLLED_STOP` |
| `INITIALIZING`/`CONTROLLED_STOP`/`NORMAL`/`FAULT` | `HEALTH_FAULT` | — | `FAULT` (원인 latch) |
| `CONTROLLED_STOP` | `RESUME_REQUESTED` | 필수 health 정상, E-Stop 해제, operator 확인 | `NORMAL` |
| `NORMAL` | `STOP_REQUESTED` | — | `CONTROLLED_STOP` |
| `NORMAL` | `LINK_LOST`/`WATCHDOG_TIMEOUT` | — | `CONTROLLED_STOP` |
| 모든 상태 | `ESTOP_ENGAGED` | 기존 fault 원인을 별도로 latch | `E_STOP_LATCHED` |
| `E_STOP_LATCHED` | `HEALTH_FAULT` | fault 원인을 latch하고 E-Stop 유지 | `E_STOP_LATCHED` |
| `E_STOP_LATCHED` | `ESTOP_RELEASED` | 모든 E-Stop 입력 해제, latched fault 없음 | `CONTROLLED_STOP` |
| `E_STOP_LATCHED` | `ESTOP_RELEASED` | 모든 E-Stop 입력 해제, latched fault 있음 | `FAULT` |
| `FAULT` | `CLEAR_FAULT` | 원인이 제거되고 health 정상 | `CONTROLLED_STOP` |

E-Stop release, fault clear, link restore는 `NORMAL`로 바로 가지 않는다. 운영자의
명시적 resume가 필요하다. HMI 연결 단절은 E-Stop이 아니라 controlled stop이다.

## Motion Interlock FSM

### 상태

- `NONE`
- `BASE_ACTIVE`
- `BASE_STOPPING`
- `ARM_ACTIVE`
- `ARM_STOPPING`

Interlock Manager 재시작 상태는 항상 `NONE`이다. Safety 상태를 소유하지 않으며,
Safety permit이 false여도 authority 상태는 정지 피드백을 받을 때까지 별도로 관리한다.

| 현재 상태 | 요청/사건 | 다음 상태 | 설명 |
|---|---|---|---|
| `NONE` | `REQUEST_BASE` | `BASE_ACTIVE` | base authority 부여 |
| `NONE` | `REQUEST_ARM` | `ARM_ACTIVE` | arm authority 부여 |
| `BASE_ACTIVE` | `REQUEST_ARM` | `BASE_STOPPING` | pending=`ARM_ACTIVE` |
| `BASE_ACTIVE` | `RELEASE` | `BASE_STOPPING` | pending=`NONE` |
| `BASE_STOPPING` | `BASE_STOPPED` | pending | 신선한 정지 피드백 필요 |
| `ARM_ACTIVE` | `REQUEST_BASE` | `ARM_STOPPING` | pending=`BASE_ACTIVE` |
| `ARM_ACTIVE` | `RELEASE` | `ARM_STOPPING` | pending=`NONE` |
| `ARM_STOPPING` | `ARM_STOPPED` | pending | 신선한 정지 피드백 필요 |
| stopping 상태 | `TRANSITION_TIMEOUT` | `NONE` | Safety fault 발생, 자동 재요청 금지 |

동일 authority 재요청은 idempotent success로 응답한다. stopping 중 다른 새 요청은
거부한다.

## Mission BT 계약

1차 B2-only Mission BT는 정적인 sequence다.

```text
MissionSequence
├─ ValidateMission
├─ NavigateToWaypoint(0)
├─ NavigateToWaypoint(1)
├─ ...
└─ NavigateToDockApproach  # return_to_dock=true일 때만
```

각 leaf는 다음 계약을 지킨다.

- `tick()`은 `RUNNING`, `SUCCESS`, `FAILURE` 중 하나를 반환한다.
- `halt()`는 idempotent하며 active action의 cancel을 요청한다.
- Mission Manager는 leaf adapter의 `halted()`가 true가 될 때까지 `PAUSING`에 머문다.
- `reset()`은 halt 완료 뒤에만 호출한다.
- 결과에는 machine-readable reason code를 포함한다.
- cancel은 failure로 기록하지 않는다. pause 과정의 정상 결과다.
- retry와 timeout은 leaf 내부에 숨기지 않고 Mission 정책으로 노출한다.

`return_to_dock=true`인 Mission은 dock approach 위치가 없으면 `READY`에 진입하지
않는다. false인 Mission은 마지막 waypoint 성공 후 `COMPLETED`가 된다.

## ROS 인터페이스 방향

최종 구현에서는 제어 상태에 `std_msgs/String`과 의미 없는 Bool을 사용하지 않는다.
공통 `shalom_interfaces` 패키지에 최소한 다음 typed interface를 둔다.

- `MissionWaypoint.msg`: waypoint ID, pose, operation, 요구 capability
- `MissionPlan.msg`: immutable plan revision, map, waypoint와 dock approach
- `ConfigureMission.srv`: plan snapshot 설정과 idempotent 응답
- `MissionControl.srv`: request ID, operator, start/pause/resume/stop/reset
- `MissionState.msg`: 상태, Mission ID, step, reason code, sequence, timestamp
- `SafetyCommand.srv`: software E-Stop, release, resume, clear fault
- `SafetyState.msg`: 상태, permit, source, reason code, transition timestamp
- `SafetyHeartbeat.msg`: source, sequence, alive, 관측 timestamp
- `AuthorityRequest.srv`: requester, base/arm/release, request ID
- `MotionAuthority.msg`: 현재/pending authority, owner, reason code
- `MotionStopped.msg`: base/arm 구분, stopped, source, sequence, timestamp

Nav2처럼 오래 걸리는 장치 동작은 ROS Action을 사용한다. 즉시 승인/거부가 필요한
운용 명령은 Service, 관측 상태와 heartbeat는 Topic을 사용한다. QoS와 필드 상세는
interface 구현 전 별도 표로 고정한다.

## 재시작 계약

| 재시작 대상 | 재시작 후 상태 |
|---|---|
| `mission_manager` | 저장 checkpoint를 읽되 `PAUSED` 또는 `READY`; 자동 이동 금지 |
| `safety_manager` | `INITIALIZING`, permit=false |
| `motion_interlock_manager` | `NONE` |
| `twist_mux` | authority를 복원하지 않으며, Safety Gate가 zero output 유지 |
| `safety_gate` | zero output |
| `hmi_bridge` | 상태를 다시 구독; 자동 resume 금지 |

Mission checkpoint에는 Mission ID, map ID, 마지막 완료 waypoint, 시작 시각과 종료
사유만 저장한다. 진행 중이던 action handle을 복원하지 않는다.

## B2 시뮬레이션 검증 기준

### 단위 테스트

- 모든 `state × event` 조합의 수락/거부와 reason code
- E-Stop release만으로 Safety Normal 또는 Mission Running이 되지 않음
- base와 arm authority가 동시에 active가 되지 않음
- `PAUSING` 동안 Mission BT가 다시 tick되지 않음
- 중단 waypoint를 재개 시 처음부터 다시 요청함
- 없는 capability가 READY 진입을 막음

### 통합 테스트

- HMI 수동 명령이 mux와 gate를 거쳐 B2를 이동시킴
- HMI 목표가 Nav2와 gate를 거쳐 B2를 이동시킴
- 수동 전환이 Nav2를 cancel하고 Mission을 PAUSED로 만듦
- HMI link loss 뒤 1초 안에 gate가 zero를 출력함
- HMI가 재연결돼도 Mission이 자동 재개되지 않음
- software E-Stop engage/release 뒤 명시적 resume가 필요함
- `hmi_bridge`, `mission_manager`, `safety_manager`, mux 종료가 fail-closed임
- LiDAR/B2 state 중단이 정의된 Safety 또는 Mission 상태를 만듦
- 도크 접근 성공을 충전 성공으로 표시하지 않음

물리 정지거리, 물리 E-Stop/STO, FR3 정지, 계단 주행, 충전 접점은 이 검증의 성공
판정에 포함하지 않는다.

## 구현 순서

1. 이 계약의 상태·이벤트·시간 기준을 리뷰하고 기준선으로 고정한다.
2. `shalom_interfaces`의 typed message/service 초안을 만든다.
3. 순수 C++ Mission/Safety/Interlock FSM과 전이표 단위 테스트를 구현한다.
4. Safety Manager, Interlock, Mux, Gate를 먼저 B2 시뮬레이터에 연결한다.
5. HMI 수동 주행과 link-loss 1초 정지를 검증한다.
6. 현재 `hmi_bridge`의 Mission orchestration을 독립 `mission_manager` 노드로 옮긴다.
7. Mission BT와 Nav2 leaf를 연결한다.
8. HMI 자율 Mission, pause/manual takeover/resume/return을 연결한다.
9. 프로세스 종료와 sensor dropout 장애 검증을 자동화한다.
10. 코드·파라미터와 진단 문서의 1초 통신 두절 계약을 계속 정합화한다.

FR3, Stair BT, 실제 docking 구현은 위 B2-only 수직 경로가 통과한 뒤 별도 단계로
진행한다.
