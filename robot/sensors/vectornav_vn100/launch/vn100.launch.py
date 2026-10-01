# Copyright (c) 2026 WeGo Robotics. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Wego-Proprietary

"""Expose the VN-100's raw accelerometer and gyro without claiming orientation."""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    config_file = PathJoinSubstitution(
        [FindPackageShare("vectornav_vn100"), "config", "vn100.yaml"])

    return LaunchDescription([
        DeclareLaunchArgument("config_file", default_value=config_file),
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
        ),
        Node(
            package="vectornav_vn100",
            executable="vn100_imu_adapter",
            namespace="vn100",
            name="imu_adapter",
            output="screen",
        ),
    ])
