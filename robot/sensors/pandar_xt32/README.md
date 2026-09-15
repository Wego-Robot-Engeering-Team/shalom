# Pandar XT32

`pandar_xt32` is the project adapter for the official Hesai ROS 2 driver.  It
does not contain a fork of the driver: that source is pinned at
`third_party/hesai_lidar_ros2`.

It publishes the standard interface expected by the rest of the robot:

| Item | Value |
|---|---|
| Point cloud | `/b2/points` (`sensor_msgs/PointCloud2`) |
| Driver frame | `pandar_xt32` |
| Stack LiDAR frame | `b2/lidar_link` |

Before commissioning, create a site-specific copy of `config/xt32.yaml` and
set the Pandar's IP/UDP/PTC ports and calibration policy to the device's web
configuration. Measure the `base_link → pandar_xt32` mount transform; the
launch defaults are only a temporary B2 mounting-position example.

```bash
ros2 launch pandar_xt32 xt32.launch.py \
  config_file:=/etc/shalom/pandar_xt32.yaml \
  x:=<metres> y:=<metres> z:=<metres> yaw:=<radians>
```

Use `lidar:=xt32` in the `robot_bringup` launches to include it in the full
stack. Ubuntu installation and NIC setup are in [docs/install.md](../../../docs/install.md).
