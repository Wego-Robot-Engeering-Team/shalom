# Robot

| 경로 | 역할 |
|---|---|
| `application/` | B2·Nav2 bring-up과 설정 |
| `lidar_slam/` | 3D LiDAR 기반 SLAM |
| `camera_streamer/` | RealSense 역할 설정과 RTSP 뷰파인더 |
| `aurora_odometry/` | Aurora S 원시 6DoF odometry 설정 |
| `bridge/` | HMI TCP ↔ ROS 2 브릿지 |

공통 계약은 `../common/`에 둔다.
