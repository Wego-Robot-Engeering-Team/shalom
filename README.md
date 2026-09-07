# 철도차량 하부점검 시스템

Unitree B2 기반 로봇 점검 시스템.

```text
robot/   자율주행·SLAM·브릿지
hmi/     관제 GUI와 개발용 testbed
common/  공통 통신 계약
docs/    운용·통신 문서
```

## 실행

```bash
source ~/shalom_ws/src/b2_simulation/mujoco/b2_mujoco/b2_env.sh
ros2 launch application b2_navigation.launch.py robot:=sim
```

HMI는 `cmake --preset dev -S hmi && cmake --build --preset dev`로 빌드한다.

문서: [설치](docs/setup.md), [SLAM](docs/slam.md),
[내비게이션](docs/navigation.md), [수동 조종](docs/teleop.md),
[통신 규약](docs/bridge_protocol.md).
