# Copyright (c) 2026 WeGo Robotics. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Wego-Proprietary

"""Run the B2 simulation as a robot-shaped HMI endpoint."""

import json
from pathlib import Path

from launch import LaunchDescription
from launch.actions import (DeclareLaunchArgument, IncludeLaunchDescription,
                            OpaqueFunction, SetEnvironmentVariable, SetLaunchConfiguration)
from launch.conditions import IfCondition
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare
from ament_index_python.packages import get_package_share_directory


def _writable_maps_dir():
    """In a symlink install, use the source maps tree, not copied package files."""
    installed = Path(get_package_share_directory("simulation_bringup")) / "maps"
    readme = installed / "README.md"
    return str(readme.resolve().parent)


def _resolve_default_map(context):
    requested = LaunchConfiguration("map").perform(context)
    if requested == "none":
        return [SetLaunchConfiguration("map", "")]
    if requested != "auto":
        return []
    maps_dir = Path(LaunchConfiguration("maps_dir").perform(context))
    setting = maps_dir / "default_map.json"
    if not setting.is_file():
        return [SetLaunchConfiguration("map", "")]
    try:
        map_id = json.loads(setting.read_text(encoding="utf-8"))["map_id"]
    except (OSError, KeyError, ValueError, TypeError) as exc:
        raise RuntimeError(f"invalid default map setting: {setting}") from exc
    if map_id == "":
        return [SetLaunchConfiguration("map", "")]
    if (not isinstance(map_id, str) or Path(map_id).name != map_id or
            map_id in (".", "..") or ".." in map_id):
        raise RuntimeError(f"invalid default map id in {setting}")
    map_yaml = maps_dir / map_id / "map.yaml"
    if not map_yaml.is_file():
        raise RuntimeError(f"default map is missing: {map_yaml}")
    return [SetLaunchConfiguration("map", str(map_yaml))]


def generate_launch_description():
    sim = FindPackageShare("simulation_bringup")
    robot = FindPackageShare("robot_bringup")
    bridge = FindPackageShare("hmi_bridge")
    mujoco = FindPackageShare("b2_mujoco")

    platform = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(PathJoinSubstitution([mujoco, "launch", "b2_sim.launch.py"])),
        launch_arguments={
            "viewer": LaunchConfiguration("viewer"),
            "scene_file": LaunchConfiguration("scene_file"),
            "ground_truth_tf": "false",
        }.items(),
    )
    navigation = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(PathJoinSubstitution([robot, "launch", "navigation.launch.py"])),
        launch_arguments={
            "use_sim_time": "true",
            "pointcloud_topic": LaunchConfiguration("pointcloud_topic"),
            "map": LaunchConfiguration("map"),
            "slam": LaunchConfiguration("slam"),
            "nav2": LaunchConfiguration("nav2"),
        }.items(),
    )
    control = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(PathJoinSubstitution([robot, "launch", "control.launch.py"])),
        launch_arguments={
            "use_sim_time": "true",
            "base_output_topic": "/cmd_vel",
            "base_odometry_topic": "/b2/odom_gt",
            "teleop_allowed_peer": "127.0.0.1",
            "robot_id": LaunchConfiguration("robot_id"),
        }.items(),
    )
    station_bridge = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(PathJoinSubstitution([bridge, "launch", "bridge.launch.py"])),
        launch_arguments={
            "config": LaunchConfiguration("bridge_config"),
            "use_sim_time": "true",
            "robot_id": LaunchConfiguration("robot_id"),
            "robot_name": LaunchConfiguration("robot_name"),
            "maps_dir": LaunchConfiguration("maps_dir"),
            "initial_map": LaunchConfiguration("map"),
        }.items(),
    )
    # The bridge calls the same B2 posture services for hardware and MuJoCo.
    # This adapter is the simulator-side implementation of that platform
    # contract; hmi_bridge has no simulation fallback.
    posture_adapter = Node(
        package="simulation_bringup",
        executable="base_posture_adapter.py",
        name="base_posture_adapter",
        output="screen",
        parameters=[{"use_sim_time": True}],
    )
    rviz = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(PathJoinSubstitution([robot, "launch", "rviz.launch.py"])),
        launch_arguments={
            "use_sim_time": "true",
            "profile": LaunchConfiguration("rviz_profile"),
        }.items(),
        condition=IfCondition(LaunchConfiguration("rviz")),
    )

    return LaunchDescription([
        DeclareLaunchArgument("domain_id", default_value="0"),
        SetEnvironmentVariable("ROS_DOMAIN_ID", LaunchConfiguration("domain_id")),
        SetEnvironmentVariable("RMW_IMPLEMENTATION", "rmw_cyclonedds_cpp"),
        SetEnvironmentVariable(
            "CYCLONEDDS_URI",
            ["file://", PathJoinSubstitution([robot, "config", "cyclonedds.xml"])]),
        DeclareLaunchArgument("robot_id", default_value="SIM-B2-1"),
        DeclareLaunchArgument("robot_name", default_value="B2 simulator"),
        DeclareLaunchArgument(
            "scene_file",
            default_value=PathJoinSubstitution([mujoco, "models", "b2_nav_scene.xml"]),
            description="MuJoCo scene containing the B2 and its test environment.",
        ),
        DeclareLaunchArgument("pointcloud_topic", default_value="/b2/points"),
        DeclareLaunchArgument("maps_dir",
                              default_value=_writable_maps_dir(),
                              description="Simulator-owned map bundle directory"),
        DeclareLaunchArgument(
            "map",
            default_value="auto",
            description="auto(기본 지도), none(SLAM), 절대 경로의 map.yaml",
        ),
        OpaqueFunction(function=_resolve_default_map),
        DeclareLaunchArgument("slam", default_value="true"),
        DeclareLaunchArgument("nav2", default_value="true"),
        DeclareLaunchArgument("viewer", default_value="true"),
        DeclareLaunchArgument("rviz", default_value="true"),
        DeclareLaunchArgument("rviz_profile", default_value="slam_nav2",
                              choices=["slam", "nav2", "slam_nav2"]),
        DeclareLaunchArgument(
            "bridge_config",
            default_value=PathJoinSubstitution([sim, "config", "bridge_sim.yaml"])),
        platform, navigation, control, posture_adapter, station_bridge, rviz,
    ])
