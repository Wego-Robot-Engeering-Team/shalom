# motion_interlock_manager

Base와 arm의 운용 권한을 하나만 허용한다. 권한 전환은 기존 축의 `stopped`
feedback을 받은 뒤에만 완료된다. 안전 상태를 소유하지 않으며, `safety_manager`와
`safety_gate`가 별도로 command를 막는다.

| Input | Value |
|---|---|
| `/motion/authority/request` | `shalom_interfaces/AuthorityRequest` service |
| `/motion/stopped` | `shalom_interfaces/MotionStopped` topic |

`/motion/authority`는 typed `shalom_interfaces/MotionAuthority`를 발행한다.

전환 시간 초과 시 authority와 pending authority를 모두 `none`으로 내린다. 이 결과는
Safety Manager에 fault로 전달돼야 하며 자동으로 이전 요청을 다시 승인하지 않는다.
