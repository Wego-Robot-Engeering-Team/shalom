# Copyright (c) 2026 WeGo Robotics. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Wego-Proprietary

"""Select Aurora odometry and/or camera, semantic and depth acquisition."""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.conditions import IfCondition
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    share = FindPackageShare("slamtec_aurora")
    config = PathJoinSubstitution(
        [share, "config", "aurora_s.yaml"])

    return LaunchDescription([
        DeclareLaunchArgument(
            "ip_address", default_value="192.168.11.1",
            description="Aurora S 유선 관리/SDK 주소"),
        DeclareLaunchArgument("odom", default_value="true",
                              description="Run the vendor odometry/status driver."),
        DeclareLaunchArgument("imaging", default_value="false",
                              description="Receive camera, semantic labels and registered depth."),
        DeclareLaunchArgument("config_file", default_value=config,
                              description="Aurora odometry and imaging node YAML."),
        DeclareLaunchArgument("frame_topic", default_value="/aurora/imaging/frame"),
        Node(
            package="slamware_ros_sdk",
            executable="slamware_ros_sdk_server_node",
            name="aurora_driver",
            parameters=[LaunchConfiguration("config_file"),
                        {"ip_address": LaunchConfiguration("ip_address")}],
            condition=IfCondition(LaunchConfiguration("odom")),
            output="screen",
            respawn=True,
            respawn_delay=3.0,
        ),
        Node(
            package="slamtec_aurora",
            executable="aurora_imaging",
            name="aurora_imaging",
            parameters=[LaunchConfiguration("config_file"), {
                "ip_address": LaunchConfiguration("ip_address"),
                "frame_topic": LaunchConfiguration("frame_topic"),
            }],
            condition=IfCondition(LaunchConfiguration("imaging")),
            output="screen",
        ),
    ])
