# Copyright (c) 2026 WeGo Robotics. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Wego-Proprietary

"""Aurora-S 사람 분할 3D/2D 점군 단독 확인."""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, GroupAction, IncludeLaunchDescription
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.conditions import IfCondition
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    share = FindPackageShare("person_perception")
    driver_share = FindPackageShare("slamtec_aurora")
    config = PathJoinSubstitution([share, "config", "person_cloud.yaml"])
    rviz_config = PathJoinSubstitution([share, "rviz", "person_cloud.rviz"])

    return LaunchDescription([
        DeclareLaunchArgument("ip_address", default_value="192.168.11.1"),
        DeclareLaunchArgument("rviz", default_value="true"),
        DeclareLaunchArgument("driver", default_value="true"),
        DeclareLaunchArgument("config_file", default_value=config),
        DeclareLaunchArgument("driver_config_file", default_value=PathJoinSubstitution([
            driver_share, "config", "aurora_s.yaml"])),
        DeclareLaunchArgument("frame_topic", default_value="/aurora/imaging/frame"),
        GroupAction(actions=[IncludeLaunchDescription(
            PythonLaunchDescriptionSource(PathJoinSubstitution([
                driver_share, "launch", "aurora_s.launch.py"])),
            condition=IfCondition(LaunchConfiguration("driver")),
            launch_arguments={
                "ip_address": LaunchConfiguration("ip_address"),
                "odom": "false",
                "imaging": "true",
                "config_file": LaunchConfiguration("driver_config_file"),
                "frame_topic": LaunchConfiguration("frame_topic"),
            }.items(),
        )], scoped=True),
        Node(
            package="person_perception",
            executable="aurora_person_cloud",
            name="aurora_person_cloud",
            parameters=[LaunchConfiguration("config_file"),
                        {"input_topic": LaunchConfiguration("frame_topic")}],
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
