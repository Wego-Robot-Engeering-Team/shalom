# Copyright (c) 2026 WeGo Robotics. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Wego-Proprietary

"""Show VN-100 accelerometer-derived vehicle roll/pitch with a car in RViz."""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription
from launch.conditions import IfCondition
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    share = FindPackageShare("vectornav_vn100")
    sensor_launch = PathJoinSubstitution([share, "launch", "vn100.launch.py"])
    rviz_config = PathJoinSubstitution([share, "rviz", "vn100.rviz"])

    return LaunchDescription([
        DeclareLaunchArgument(
            "port",
            default_value="/dev/serial/by-id/usb-FTDI_USB-RS232-WE_AV0LFM92-if00-port0"),
        DeclareLaunchArgument("start_sensor", default_value="true"),
        DeclareLaunchArgument("rviz", default_value="true"),
        DeclareLaunchArgument("imu_topic", default_value="/vn100/imu/data_ned"),
        IncludeLaunchDescription(
            PythonLaunchDescriptionSource(sensor_launch),
            launch_arguments={"port": LaunchConfiguration("port")}.items(),
            condition=IfCondition(LaunchConfiguration("start_sensor")),
        ),
        Node(
            package="vectornav_vn100",
            executable="vn100_attitude_visualizer",
            name="attitude_visualizer",
            parameters=[{"imu_topic": LaunchConfiguration("imu_topic")}],
            output="screen",
        ),
        Node(
            package="tf2_ros",
            executable="static_transform_publisher",
            name="vn100_visualization_tf",
            arguments=["--x", "0", "--y", "0", "--z", "0",
                       "--roll", "0", "--pitch", "0", "--yaw", "0",
                       "--frame-id", "vn100_world",
                       "--child-frame-id", "vn100_visualization"],
            condition=IfCondition(LaunchConfiguration("rviz")),
            output="screen",
        ),
        Node(
            package="rviz2",
            executable="rviz2",
            name="vn100_rviz",
            arguments=["-d", rviz_config],
            condition=IfCondition(LaunchConfiguration("rviz")),
            output="screen",
        ),
    ])
