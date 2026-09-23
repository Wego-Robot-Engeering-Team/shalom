# Copyright (c) 2026 WeGo Robotics. All rights reserved.

"""Expose a Pandar XT32 through the robot's standard LiDAR interface.

The official Hesai driver stays unmodified in third_party.  Its native
`/lidar_points` topic and `pandar_xt32` frame are adapted here to the interface
used by perception, SLAM, and Nav2: `/b2/points` and `b2/lidar_link`.

Before using hardware, copy the installed xt32.yaml to a site-specific location
and set the LiDAR's actual IP, ports, and calibration options.  The mount values
are examples only and must be measured on the commissioned robot.
"""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    package_share = FindPackageShare("pandar_xt32")
    config_file = PathJoinSubstitution([package_share, "config", "xt32.yaml"])

    driver = Node(
        package="hesai_ros_driver",
        executable="hesai_ros_driver_node",
        name="hesai_ros_driver_node",
        output="screen",
        parameters=[{"config_path": LaunchConfiguration("config_file")}],
        remappings=[("/lidar_points", LaunchConfiguration("points_topic"))],
    )

    # The driver frame is kept vendor-specific; this transform is the one place
    # where a vehicle's physical installation enters the generic autonomy stack.
    mount = Node(
        package="tf2_ros",
        executable="static_transform_publisher",
        name="pandar_xt32_mount",
        arguments=[
            "--x", LaunchConfiguration("x"),
            "--y", LaunchConfiguration("y"),
            "--z", LaunchConfiguration("z"),
            "--roll", LaunchConfiguration("roll"),
            "--pitch", LaunchConfiguration("pitch"),
            "--yaw", LaunchConfiguration("yaw"),
            "--frame-id", LaunchConfiguration("base_frame"),
            "--child-frame-id", "pandar_xt32",
        ],
        output="screen",
    )
    alias = Node(
        package="tf2_ros",
        executable="static_transform_publisher",
        name="pandar_xt32_frame_alias",
        arguments=[
            "--frame-id", "pandar_xt32",
            "--child-frame-id", LaunchConfiguration("lidar_frame"),
        ],
        output="screen",
    )

    return LaunchDescription([
        DeclareLaunchArgument("config_file", default_value=config_file,
                              description="Hesai YAML configuration; use a commissioned site copy."),
        DeclareLaunchArgument("points_topic", default_value="/b2/points"),
        DeclareLaunchArgument("base_frame", default_value="base_link"),
        DeclareLaunchArgument("lidar_frame", default_value="b2/lidar_link"),
        DeclareLaunchArgument("x", default_value="0.34218",
                              description="Measured Pandar origin in base_frame, metres."),
        DeclareLaunchArgument("y", default_value="0.0"),
        DeclareLaunchArgument("z", default_value="0.20"),
        DeclareLaunchArgument("roll", default_value="0.0"),
        DeclareLaunchArgument("pitch", default_value="0.0"),
        DeclareLaunchArgument("yaw", default_value="0.0"),
        driver, mount, alias,
    ])
