# Copyright (c) 2026 WeGo Robotics. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Wego-Proprietary

"""L5: HMI TCP, E-Stop TCP and teleoperation UDP endpoints."""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, GroupAction, IncludeLaunchDescription
from launch.conditions import IfCondition
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    bridge_package = FindPackageShare("hmi_bridge")
    tcp = GroupAction(
        actions=[IncludeLaunchDescription(
            PythonLaunchDescriptionSource(
                PathJoinSubstitution([bridge_package, "launch", "bridge.launch.py"])),
            launch_arguments={
                "config": LaunchConfiguration("bridge_config"),
                "initial_map": LaunchConfiguration("map"),
                **{name: LaunchConfiguration(name) for name in (
                    "use_sim_time", "robot_id", "robot_name", "maps_dir",
                    "robot_data_dir", "estop_port",
                )},
            }.items(),
        )],
        scoped=True,
        condition=IfCondition(LaunchConfiguration("bridge")),
    )
    return LaunchDescription([
        DeclareLaunchArgument("use_sim_time", default_value="false"),
        DeclareLaunchArgument("robot_id", description="Robot identity supplied by bringup"),
        DeclareLaunchArgument("robot_name", default_value="1호기"),
        DeclareLaunchArgument("maps_dir", default_value="/var/lib/shalom/maps"),
        DeclareLaunchArgument("robot_data_dir", default_value="/var/lib/shalom"),
        DeclareLaunchArgument("map", default_value=""),
        DeclareLaunchArgument("bridge", default_value="true"),
        DeclareLaunchArgument("bridge_config", default_value=PathJoinSubstitution([
            bridge_package, "config", "bridge.yaml",
        ])),
        DeclareLaunchArgument("estop_port", default_value="9091"),
        DeclareLaunchArgument("teleop_udp_port", default_value="9090"),
        DeclareLaunchArgument("teleop_allowed_peer", default_value=""),
        Node(
            package="teleop_bridge", executable="teleop_bridge_node",
            name="teleop_bridge", output="screen",
            parameters=[{
                "use_sim_time": ParameterValue(LaunchConfiguration("use_sim_time"), value_type=bool),
                "udp_port": ParameterValue(LaunchConfiguration("teleop_udp_port"), value_type=int),
                "allowed_peer": LaunchConfiguration("teleop_allowed_peer"),
                "robot_id": LaunchConfiguration("robot_id"),
                "output_topic": "/motion/teleop/cmd_vel",
            }],
        ),
        tcp,
    ])
