# motion_interlock_manager

Base와 arm의 운용 권한을 하나만 허용한다. 권한 전환은 기존 축의 `stopped`
feedback을 받은 뒤에만 완료된다. 안전 상태를 소유하지 않으며, `safety_manager`와
`safety_gate`가 별도로 command를 막는다.

| Input | Value |
|---|---|
| `/motion/authority/request` | `shalom_interfaces/AuthorityRequest` service |
| `/motion/stopped` | `shalom_interfaces/MotionStopped` topic |

`/motion/authority`는 typed `shalom_interfaces/MotionAuthority`를 발행한다.

`base_motion_monitor_node`는 Safety Gate의 최종 base 명령과 B2 odometry를 함께
확인해 `/motion/stopped`의 `BASE` 상태를 발행한다. 명령과 odometry가 모두
신선하고, 명령이 0이며, 측정 속도가 임계값 이하인 상태가 설정된 settling
시간 동안 유지돼야만 `stopped=true`가 된다. 시뮬레이션은 `/b2/odom_gt`,
실물은 B2 상태 드라이버의 `/b2/odom`을 사용한다.

전환 시간 초과 시 authority와 pending authority를 모두 `none`으로 내린다. 이 결과는
Safety Manager에 fault로 전달돼야 하며 자동으로 이전 요청을 다시 승인하지 않는다.
