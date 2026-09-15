# motion_interlock_manager

Base와 arm의 운용 권한을 하나만 허용한다. 권한 전환은 기존 축의 `stopped`
feedback을 받은 뒤에만 완료된다. 안전 상태를 소유하지 않으며, `safety_manager`와
`safety_gate`가 별도로 command를 막는다.

| Input | Value |
|---|---|
| `/motion/request_authority` | `base`, `arm`, `release` |
| `/motion/base/stopped` | base 정지 확인 |
| `/motion/arm/stopped` | arm 정지 확인 |

`/motion/authority`는 `none`, `base_active`, `base_stopping`, `arm_active`,
`arm_stopping` 중 하나를 발행한다.
