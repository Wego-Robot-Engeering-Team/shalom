# safety_gate

`safety_gate`는 motion command를 final driver topic으로 보내기 전 마지막으로
검사한다. typed `/safety/state`가 `NORMAL`이고 `motion_permitted=true`이며
typed `/motion/authority`가 `BASE_ACTIVE`일 때만 base 명령을 통과시킨다.
또한 HMI bridge의 `/motion/base/inhibit`와 base source manager의
`/motion/base_source/inhibit`가 모두 250 ms 이내에 갱신되고 `false`여야 한다.
어느 한쪽이라도 없거나, 오래되거나, `true`이면 gate는 매 주기 0을 출력한다.
두 inhibit는 독립적으로 적용된다. 어느 쪽이든 값이 바뀌거나 heartbeat가
끊긴 뒤 복구되면 이전 base 명령을 폐기하며, 해제 후 새 mux 명령을 기다린다.
이 제어 경로는 물리 E-Stop을 대체하지 않는다.
`NORMAL`이어도 permit이 false면 0 `Twist`를 발행한다. `CONTROLLED_STOP`과 `FAULT`에서는
0 `Twist`를 발행하고, `E_STOP_LATCHED` 또는 safety 상태 timeout에서는 발행을
차단해 B2 드라이버의 command timeout으로 정지시킨다. authority snapshot이 기본
500 ms 안에 갱신되지 않아도 fail-closed로 0 `Twist`를 출력한다.

기본 `output_base_topic`은 단독 실행을 위한 `/motion/safe/cmd_vel`이다.
`robot_bringup/control.launch.py`는 모든 command source를 mux에 모은 뒤 이 출력을
실기와 B2 시뮬레이션의 `/cmd_vel`로 연결한다.

FR3 position command는 0 `JointState`로 안전하게 멈출 수 없다. 그래서
`arm_output_enabled` 기본값은 `false`이며, FR3의 stop/mode 인터페이스를 연결하기 전에는
arm command를 forward하지 않는다.
