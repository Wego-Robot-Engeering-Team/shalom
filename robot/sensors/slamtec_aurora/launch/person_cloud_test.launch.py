# Copyright (c) 2026 WeGo Robotics. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Wego-Proprietary

"""Aurora-S 사람 분할 3D/2D 점군 단독 확인."""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.conditions import IfCondition
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    share = FindPackageShare("slamtec_aurora")
    config = PathJoinSubstitution([share, "config", "aurora_s.yaml"])
    rviz_config = PathJoinSubstitution([share, "rviz", "person_cloud.rviz"])

    return LaunchDescription([
        DeclareLaunchArgument("ip_address", default_value="192.168.11.1"),
        DeclareLaunchArgument("rviz", default_value="true"),
        Node(
            package="slamtec_aurora",
            executable="aurora_person_cloud",
            name="aurora_person_cloud",
            parameters=[config, {"ip_address": LaunchConfiguration("ip_address")}],
            output="screen",
        ),
        Node(
            package="rviz2",
            executable="rviz2",
            name="aurora_person_rviz",
            arguments=["-d", rviz_config],
            condition=IfCondition(LaunchConfiguration("rviz")),
            output="screen",
        ),
    ])
