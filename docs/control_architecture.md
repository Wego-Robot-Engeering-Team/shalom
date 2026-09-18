# 제어·안전 통신 구조

```text
HMI ── TCP 9090 ── hmi_bridge ── mission_manager / safety_manager
HMI ── UDP 9090 ── teleop_bridge ── motion_mux ── safety_gate ── B2 driver

HMI arm / FR3 BT ── joint_mux ── safety_gate ── FR3 driver
```

TCP와 UDP는 같은 숫자 포트를 써도 독립된 전송 계층이므로 충돌하지 않는다.
외부 방화벽 규칙은 승인된 HMI에서 들어오는 `9090/tcp`, `9090/udp`만 허용한다.

## TCP: 신뢰성 있는 운영 경로

`hmi_bridge`는 상태, 지도, 미션, 설정, 촬영, E-Stop을 담당한다. E-Stop은
`/safety/software_estop_active`로 전달되고, UDP 경로로는 절대 전달하지 않는다.
HMI 목록의 생존 확인도 TCP 9090의 짧은 `INSPECTION-PRESENCE/1` probe를 쓴다.

## UDP: deadman 수동 주행만

`teleop_bridge`는 다음 JSON datagram만 받는다.

```json
{"v":1,"t":"teleop","robot":"R1","seq":42,"deadman":true,
 "vx":0.25,"vy":0.0,"wz":0.10}
```

- `allowed_peer`와 `robot`이 맞아야 한다.
- sequence는 단조 증가해야 한다. 지연·재전송 패킷은 버린다.
- 300 ms 동안 갱신되지 않으면 teleop lease가 만료되어 mux 우선권을 놓는다.
- UDP는 목적지·정지·E-Stop·팔 제어에 쓰지 않는다.

## 최종 명령 경로

`motion_mux` 우선순위는 `teleop > mission > stair > dock > nav`다. `joint_mux`는
`teleop > manual_hold > fr3_bt` 순서다. 두 mux의 출력은 모두 `safety_gate`를
통과한다. safety gate는 safety permit, authority, command lease를 다시 확인하며,
base가 허용되지 않을 때에는 0 Twist만 낸다.

FR3는 위치 명령에 임의의 0 JointState를 보내는 방식으로 정지하면 안 된다. vendor
stop/mode 인터페이스가 연결되기 전까지 arm gate 출력은 비활성 상태를 유지한다.

## 남은 하드웨어 통합

- physical E-Stop 입력을 `/safety/physical_estop_active`에 연결
- B2/FR3 driver가 각각 `/motion/safe/cmd_vel`, `/motion/safe/arm/joint_command`만
  소비하도록 연결
- watchdog의 comm·sensor health·process check를 `/safety/event`의 `health_fault`로
  연결
