# teleop_bridge

ROS 쪽 teleop ingress를 deadman과 300 ms lease로 제한해
`/motion/base/cmd_vel/teleop`로 내보낸다. deadman이 풀리거나 lease가 만료되면 이 노드는
아무 command도 발행하지 않아 mux의 teleop source가 만료된다. 0 Twist를 계속 내보내면
teleop이 최고 우선순위를 계속 점유하는 문제가 생기기 때문이다. UDP wire decoder는 아직 연결하지 않았다.
그 decoder는 HMI와 함께 버전·sequence·인증 규약을 확정한 뒤 이 노드의
`/teleop/input/*` 입력으로 연결한다.
