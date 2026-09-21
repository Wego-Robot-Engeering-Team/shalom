# safety_gate

`safety_gate`는 motion command를 final driver topic으로 보내기 전 마지막으로
검사한다. typed `/safety/state`의 motion permit이 만료되거나 typed
`/motion/authority`가 맞지 않으면 base에는 0 `Twist`만 발행한다.

기본 `output_base_topic`은 단독 실행을 위한 `/motion/safe/cmd_vel`이다.
`robot_bringup/control.launch.py`는 모든 command source를 mux에 모은 뒤 이 출력을
실기와 B2 시뮬레이션의 `/cmd_vel`로 연결한다.

FR3 position command는 0 `JointState`로 안전하게 멈출 수 없다. 그래서
`arm_output_enabled` 기본값은 `false`이며, FR3의 stop/mode 인터페이스를 연결하기 전에는
arm command를 forward하지 않는다.
