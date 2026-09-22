# teleop_bridge

ROS 쪽 teleop ingress를 deadman과 300 ms lease로 제한해
`/motion/teleop/cmd_vel`로 내보낸다. deadman이 풀리거나 lease가 만료되면 이 노드는
아무 command도 발행하지 않아 mux의 teleop source가 만료된다. 0 Twist를 계속 내보내면
teleop이 최고 우선순위를 계속 점유하는 문제가 생기기 때문이다. HMI UDP 입력은
version, robot ID, sequence와 허용 peer를 검사한 뒤 같은 lease에
넣는다. `/teleop/input/*` ROS 입력은 replay test와 로봇 탑재 조이스틱용으로 유지한다.
