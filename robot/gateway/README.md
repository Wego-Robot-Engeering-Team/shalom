# Gateway

외부 HMI·SDK·운영자 입력을 로봇 내부 ROS 2 인터페이스로 바꾸는 경계다. 이
디렉터리는 안전 판단이나 최종 구동 명령을 소유하지 않는다.

| 노드 | 패키지 | 역할 |
| --- | --- | --- |
| `hmi_bridge` | `hmi_bridge` | TCP 9090: HMI 지도·미션·상태 API |
| `estop_bridge` | `estop_bridge` | TCP 9091: HMI E-Stop·전용 heartbeat를 typed 안전 API로 전달 |
| `teleop_bridge` | `teleop_bridge` | HMI UDP 수동 조작 검증과 deadman lease |

`teleop_bridge`는 ROS/colcon의 패키지 탐색 규칙 때문에 `hmi_bridge` 패키지의
하위가 아니라 sibling 패키지로 둔다. 세 노드는 모두 HMI gateway 영역이다.

안전 상태 판정은 `../control/safety_manager`, 최종 명령 차단은
`../control/safety_gate`가 담당한다.
