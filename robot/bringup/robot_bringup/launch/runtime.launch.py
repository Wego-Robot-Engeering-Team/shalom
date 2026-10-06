# Copyright (c) 2026 WeGo Robotics. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Wego-Proprietary

"""Shared L2-L4 runtime for physical robots and simulation."""

from launch import LaunchDescription
from launch.actions import (DeclareLaunchArgument, GroupAction, IncludeLaunchDescription,
                            OpaqueFunction)
from launch.conditions import IfCondition
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.substitutions import FindPackageShare
from robot_bringup.map_selection import resolve_default_map as _resolve_default_map


def _subsystem(package, filename, arguments, condition=None):
    # Child launch defaults and generated Nav2 params remain inside their scope.
    return GroupAction(
        actions=[IncludeLaunchDescription(
            PythonLaunchDescriptionSource(PathJoinSubstitution([
                FindPackageShare(package), "launch", filename,
            ])),
            launch_arguments=arguments.items(),
        )],
        scoped=True,
        condition=condition,
    )


def generate_launch_description():
    pkg = FindPackageShare("robot_bringup")
    argument = LaunchConfiguration
    navigation = _subsystem("navigation", "navigation.launch.py", {
        name: argument(name) for name in ("use_sim_time", "pointcloud_topic", "map", "slam", "nav2")
    })
    system = _subsystem("robot_bringup", "system.launch.py", {
        name: argument(name) for name in (
            "use_sim_time", "mission_manager_config", "base_output_topic",
            "base_odometry_topic", "require_external_heartbeat",
        )
    })
    control = _subsystem("robot_bringup", "control.launch.py", {
        name: argument(name) for name in ("use_sim_time", "twist_mux_config", "base_output_topic")
    })
    communication = _subsystem("robot_bringup", "communication.launch.py", {
        name: argument(name) for name in (
            "use_sim_time", "robot_id", "robot_name", "maps_dir", "robot_data_dir",
            "map", "bridge", "bridge_config", "estop_port", "teleop_udp_port", "teleop_allowed_peer",
        )
    })
    rviz = _subsystem("robot_bringup", "rviz.launch.py", {
        "use_sim_time": argument("use_sim_time"), "profile": argument("rviz_profile"),
    }, condition=IfCondition(argument("rviz")))
    return LaunchDescription([
        DeclareLaunchArgument("use_sim_time", default_value="false"),
        DeclareLaunchArgument("robot_id", description="Robot identity supplied by the entrypoint"),
        DeclareLaunchArgument("robot_name", default_value="1호기"),
        DeclareLaunchArgument("pointcloud_topic", default_value="/b2/points"),
        DeclareLaunchArgument("base_output_topic", default_value="/cmd_vel"),
        DeclareLaunchArgument("base_odometry_topic", default_value="/b2/odom"),
        DeclareLaunchArgument("maps_dir", default_value="/var/lib/shalom/maps"),
        DeclareLaunchArgument("robot_data_dir", default_value="/var/lib/shalom"),
        DeclareLaunchArgument("map", default_value="auto"),
        DeclareLaunchArgument("slam", default_value="true"),
        DeclareLaunchArgument("nav2", default_value="true"),
        DeclareLaunchArgument("mission_manager_config", default_value=PathJoinSubstitution([
            pkg, "config", "mission_manager.yaml",
        ])),
        DeclareLaunchArgument("twist_mux_config", default_value=PathJoinSubstitution([
            pkg, "config", "twist_mux.yaml",
        ])),
        DeclareLaunchArgument("require_external_heartbeat", default_value="true"),
        DeclareLaunchArgument("bridge", default_value="true"),
        DeclareLaunchArgument("bridge_config", default_value=PathJoinSubstitution([
            FindPackageShare("hmi_bridge"), "config", "bridge.yaml",
        ])),
        DeclareLaunchArgument("estop_port", default_value="9091"),
        DeclareLaunchArgument("teleop_udp_port", default_value="9090"),
        DeclareLaunchArgument("teleop_allowed_peer", default_value=""),
        DeclareLaunchArgument("rviz", default_value="false"),
        DeclareLaunchArgument("rviz_profile", default_value="slam_nav2",
                              choices=["slam", "nav2", "slam_nav2"]),
        OpaqueFunction(function=_resolve_default_map),
        navigation, system, control, communication, rviz,
    ])
