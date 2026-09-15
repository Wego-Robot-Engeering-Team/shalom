# motion_mux

Base velocity source의 freshness lease를 비교해 하나만 `/motion/base/cmd_vel`로
보낸다. 기본 우선순위는 `teleop > mission > stair > dock > nav`이며 각 source와
output topic, lease는 launch parameter로 바꿀 수 있다.

이 패키지는 base Twist만 다룬다. FR3 position command는 실제 driver의 stop/mode
계약이 확정된 뒤 별도 arm command router로 추가한다.
