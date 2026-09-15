# Third-party repositories

`shalom` is the integration root. Every component with an independent Git
history lives below this directory as a submodule, including Wego-maintained
components that are developed and released separately from `shalom`.

| Directory | Responsibility | Upstream owner |
|---|---|---|
| `b2_driver` | Unitree B2 hardware driver | Wego |
| `b2_simulation` | B2 MuJoCo simulation and policy | Wego |
| `frcobot_ros2` | FAIRINO FR3 ROS 2 driver and description | FAIR INNOVATION |
| `ground_segmentation*` | Ground segmentation library and ROS 2 wrapper | DFKI RIC |
| `kiss_icp` | LiDAR odometry | PRBonn |
| `aurora_ros` | SLAMTEC Aurora ROS 2 driver | SLAMTEC |
| `hesai_lidar_ros2` | Hesai Pandar ROS 2 driver (including XT32) | Hesai Technology |
| `librealsense` | Intel RealSense SDK source and UDEV rules | Intel |
| `nav2_ground_consistency_costmap_plugin` | Nav2 costmap plugin | DFKI RIC |

The superproject records an exact commit for every entry. Clone and restore the
complete source tree with:

```bash
git clone --recurse-submodules <shalom-url>
git submodule update --init --recursive
```

Do not commit product-specific launch files or configuration into a submodule;
keep that integration in `shalom/robot`. To change a dependency, commit and
push the change in its own repository first, then update only its gitlink in
`shalom`:

```bash
git -C third_party/<name> switch <branch>
git -C third_party/<name> pull --ff-only
git add third_party/<name>
```

`scripts/install.sh` applies the currently required Aurora Jazzy compatibility
edit and creates `librealsense/COLCON_IGNORE` locally. Those generated changes
must not be committed to the upstream repositories.
