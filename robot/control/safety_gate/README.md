# safety_gate

`safety_gate`는 motion command를 final driver topic으로 보내기 전 마지막으로
검사한다. `/safety/motion_permitted` heartbeat가 만료되거나 authority가 맞지 않으면
base에는 0 `Twist`만 발행한다.

기본 `output_base_topic`은 `/motion/safe/cmd_vel`이다. 실기에서 `/cmd_vel`로 연결하는
작업은 command source가 전부 mux를 거쳐 gate로 들어오는 것을 확인한 뒤에만 한다.

FR3 position command는 0 `JointState`로 안전하게 멈출 수 없다. 그래서
`arm_output_enabled` 기본값은 `false`이며, FR3의 stop/mode 인터페이스를 연결하기 전에는
arm command를 forward하지 않는다.
