# HMI 브릿지

관제 시스템과 로봇측 ROS 2 스택 사이의 경계 노드.

관제 PC 에는 ROS 2 를 설치하지 않는다. DDS 영역은 이 노드에서 끝나고,
관제와는 raw TCP 단일 연결로만 통신한다. 통신 규약은
[`bridge_protocol.md`](../../../docs/bridge_protocol.md)를 따른다.

## 구성

```
hmi_bridge/          ROS 2 패키지 (ament_cmake)
  include/hmi_bridge/
    tcp_server.hpp        관제 연결 수락, 프레이밍 — ROS 2 비의존
    envelope.hpp          JSON 봉투
    bridge_node.hpp       ROS 2 노드
  src/
  config/bridge.yaml      파라미터
  launch/bridge.launch.py

```

프레이밍 구현은 `../../../common/protocol/include/inspection/framing.hpp` 하나뿐이며 관제와
공유한다. 같은 바이트 배치를 두 번 구현하면 언젠가 어긋나고, 그 어긋남은
빌드가 아니라 현장에서 드러난다.

## 빌드

```bash
# ROS 2 워크스페이스에서
colcon build --packages-select hmi_bridge
```

의존성: `rclcpp`, `tf2_ros`, `nav_msgs`, `sensor_msgs`, `nlohmann-json-dev`

## 전송 계층 테스트

소켓·스레드·프레이밍이 얽히는 부분이 실제 위험이 있는 곳이다.
테스트 대상 코드는 ROS에 의존하지 않으며 패키지의 CMake/CTest로 빌드한다.

```bash
cd ~/shalom_ws
colcon test --packages-select hmi_bridge
colcon test-result --verbose
```

## 안전에 관한 책임 분담

안전 정책은 `control/safety_manager`로 분리할 예정이다. 현재 해당 노드는
구현되지 않았으므로 이 구조만으로 비상정지·통신 두절 시 정지가 보장되지는 않는다.
브릿지는 생존 신호(`~/link_alive`, 즉 `/hmi_bridge/link_alive`)를 발행한다.

분리한 이유는 **이 노드가 죽어도 로봇이 서야 하기 때문**이다. 정지 판단이
여기 있으면 세그폴트 한 번이 곧 감시자 없는 주행이 된다. 생존 신호를 발행하고
그 부재를 다른 프로세스가 근거로 삼게 하면, 실패가 구조적으로 안전해진다.

같은 이유로 launch 파일에서 안전 노드를 함께 띄우지 않는다.

## 관제는 하나만 붙는다

두 번째 접속은 수락 즉시 닫는다. 관제 두 대가 동시에 붙으면 "명령이 충돌할 때
어느 쪽이 이기는가" 라는 질문이 생기는데, 이 시스템에는 그 답이 없다.
거절하는 편이 정직하고, 조작자도 이유를 알 수 있다.

## 미완성 부분

`bridge_node.cpp` 의 `TODO(integration)` 표시 지점:

- Nav2 `NavigateToPose` 액션 클라이언트 연결
- MoveIt2 로봇팔 명령 전달
- OccupancyGrid → PNG 인코딩 (행 순서 반전 필요, 프로토콜 §2.2)
- 야코비안 기반 조작성 지수 계산
- 센서별 실측 주기 측정과 끊김 판정

이 부분들은 ROS 2 환경에서 빌드·검증해야 한다.
