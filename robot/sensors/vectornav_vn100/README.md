# VN-100 IMU

`robot/third_party/vectornav`의 ROS 2 드라이버를 수정하지 않고 사용한다.
이 패키지는 드라이버의 raw 가속도·각속도를 `/vn100/imu/data_ned`
(`sensor_msgs/Imu`)로 전달한다. raw 데이터에는 자세가 없으므로
`orientation_covariance[0]`을 `-1`로 표시한다.

현재 출력은 VN-100의 NED 좌표계다. 좌표계 변환, 장착 TF, 위치추정 융합은
실측 후 연결한다. 기존 B2 IMU `/b2/imu/data`는 그대로 유지한다.

```bash
source /opt/ros/jazzy/setup.bash
source ~/shalom_ws/install/setup.bash
ros2 launch vectornav_vn100 vn100.launch.py port:=/dev/ttyUSB0
ros2 topic echo /vn100/imu/data_ned
```

`config/vn100.yaml`의 시리얼 속도와 출력 포트는 실물 설정에 맞춰 확인한다.
운용 시에는 USB 어댑터의 식별값으로 `/dev/ttyUSB_VN100` 심볼릭 링크를 만들고,
로봇 서비스 계정에 해당 장치 접근 권한을 부여한다.
