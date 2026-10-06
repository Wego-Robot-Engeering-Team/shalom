# Copyright (c) 2026 WeGo Robotics. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Wego-Proprietary

"""Inspect XT32 point clouds in the sensor frame."""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription
from launch.conditions import IfCondition
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    share = FindPackageShare("pandar_xt32")
    return LaunchDescription([
        DeclareLaunchArgument("config_file", default_value=PathJoinSubstitution(
            [share, "config", "xt32.yaml"])),
        DeclareLaunchArgument("start_sensor", default_value="true"),
        DeclareLaunchArgument("points_topic", default_value="/b2/points"),
        DeclareLaunchArgument("fixed_frame", default_value="pandar_xt32"),
        IncludeLaunchDescription(
            PythonLaunchDescriptionSource(PathJoinSubstitution(
                [share, "launch", "xt32.launch.py"])),
            launch_arguments={
                "config_file": LaunchConfiguration("config_file"),
                "points_topic": LaunchConfiguration("points_topic"),
                # Standalone sensor preview: no vehicle mounting offset.
                "base_frame": "xt32_preview_origin",
                "x": "0.0", "y": "0.0", "z": "0.0",
                "roll": "0.0", "pitch": "0.0", "yaw": "0.0",
            }.items(),
            condition=IfCondition(LaunchConfiguration("start_sensor")),
        ),
        Node(
            package="rviz2",
            executable="rviz2",
            name="xt32_rviz",
            arguments=["-d", PathJoinSubstitution([share, "rviz", "xt32.rviz"]),
                       "-f", LaunchConfiguration("fixed_frame")],
            remappings=[("/b2/points", LaunchConfiguration("points_topic"))],
            output="screen",
        ),
    ])
