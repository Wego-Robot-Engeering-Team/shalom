# 수동 조종

시뮬레이터나 실기가 떠 있는 상태에서, 별도 터미널로 실행한다.

```bash
source /opt/ros/jazzy/setup.bash
source ~/shalom_ws/install/setup.bash

ros2 run application control_gui
```

방향 버튼은 누르고 있는 동안에만 움직이고, 떼면 즉시 정지한다. 키보드 `W`/`A`/`S`/`D`와
스페이스바 정지도 지원한다.

기본 `/cmd_vel` 대신 다른 토픽을 쓰려면 `--topic`을 지정한다.

```bash
ros2 run application control_gui --topic /b2/cmd_vel
```

`geometry_msgs/Twist`를 발행한다. Nav2도 최종적으로 `/cmd_vel`을 발행하므로, 수동
조종 전에는 현재 목표를 취소하거나 Nav2를 끈 상태에서 사용한다. 두 명령원을 동시에
쓰면 마지막에 도착한 명령이 로봇을 움직인다.

짧은 스크립트 조종은 다음처럼 한다.

```bash
ros2 run application drive forward --duration 2
```
