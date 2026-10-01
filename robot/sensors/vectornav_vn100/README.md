# VN-100 IMU

출력: `/vn100/imu/data_ned` (`sensor_msgs/Imu`)

자세값은 제공하지 않으며 `orientation_covariance[0] = -1`이다.

## 실행
```bash
source /opt/ros/jazzy/setup.bash
source ~/shalom_ws/install/setup.bash
ros2 launch vectornav_vn100 vn100.launch.py
ros2 topic echo /vn100/imu/data_ned
```

## 설정
기본 포트: `/dev/serial/by-id/usb-FTDI_USB-RS232-WE_AV0LFM92-if00-port0`.
다른 장치를 쓰면 `port:=<장치 경로>`를 지정한다. 실기 bringup에서는
`vn100:=true`를 사용한다. 실행 계정에는 시리얼 장치 접근 권한이 필요하다.
