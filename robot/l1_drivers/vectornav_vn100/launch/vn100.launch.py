# Copyright (c) 2026 WeGo Robotics. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Wego-Proprietary

"""Expose the VN-100's raw accelerometer and gyro without claiming orientation."""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.conditions import IfCondition
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    share = FindPackageShare("vectornav_vn100")
    config_file = PathJoinSubstitution(
        [share, "config", "vn100.yaml"])

    return LaunchDescription([
        DeclareLaunchArgument("config_file", default_value=config_file),
        DeclareLaunchArgument("start_sensor", default_value="true",
                              description="Start the serial driver and IMU adapter."),
        DeclareLaunchArgument("rviz", default_value="false",
                              description="Show the vehicle roll/pitch viewer."),
        DeclareLaunchArgument("imu_topic", default_value="/vn100/imu/data_ned",
                              description="IMU topic used by the RViz visualizer."),
        DeclareLaunchArgument(
            "port",
            default_value="/dev/serial/by-id/usb-FTDI_USB-RS232-WE_AV0LFM92-if00-port0"),
        Node(
            package="vectornav_driver",
            executable="vectornav_driver_node",
            namespace="vn100",
            name="vectornav_driver_node",
            output="screen",
            parameters=[LaunchConfiguration("config_file"),
                        {"port": LaunchConfiguration("port")}],
            condition=IfCondition(LaunchConfiguration("start_sensor")),
        ),
        Node(
            package="vectornav_vn100",
            executable="vn100_imu_adapter",
            namespace="vn100",
            name="imu_adapter",
            output="screen",
            condition=IfCondition(LaunchConfiguration("start_sensor")),
        ),
        Node(
            package="vectornav_vn100",
            executable="vn100_attitude_visualizer",
            name="attitude_visualizer",
            parameters=[{"imu_topic": LaunchConfiguration("imu_topic")}],
            condition=IfCondition(LaunchConfiguration("rviz")),
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
            arguments=["-d", PathJoinSubstitution([share, "rviz", "vn100.rviz"])],
            condition=IfCondition(LaunchConfiguration("rviz")),
            output="screen",
        ),
    ])
