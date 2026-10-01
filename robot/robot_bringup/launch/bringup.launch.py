# Copyright (c) 2026 WeGo Robotics. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Wego-Proprietary

"""Run the physical robot stack; it never starts MuJoCo or test sensors."""

import json
from pathlib import Path

import yaml
from launch import LaunchDescription
from launch.actions import (DeclareLaunchArgument, IncludeLaunchDescription,
                            OpaqueFunction, SetLaunchConfiguration)
from launch.conditions import IfCondition
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.substitutions import FindPackageShare


def _robot_id_from_metadata():
    metadata_path = Path(__file__).resolve().parent.parent.parent / "config" / "robot_metadata.yaml"
    with metadata_path.open(encoding="utf-8") as metadata_file:
        metadata = yaml.safe_load(metadata_file) or {}
    robot_id = metadata.get("robot", {}).get("id", "")
    if not robot_id:
        raise RuntimeError(f"robot id is missing from {metadata_path}")
    return str(robot_id)


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
    pkg = FindPackageShare("robot_bringup")
    bridge = FindPackageShare("hmi_bridge")
    pandar_xt32 = FindPackageShare("pandar_xt32")
    vectornav_vn100 = FindPackageShare("vectornav_vn100")

    platform = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(PathJoinSubstitution([pkg, "launch", "platform.launch.py"])),
        launch_arguments={
            name: LaunchConfiguration(name)
            for name in ("domain_id", "network_interface", "pointcloud_topic", "lidar",
                         "xt32_config_file", "xt32_x", "xt32_y", "xt32_z", "xt32_roll",
                         "xt32_pitch", "xt32_yaw", "aurora", "aurora_ip", "vn100",
                         "vn100_config_file", "vn100_port")
        }.items(),
    )
    navigation = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(PathJoinSubstitution([pkg, "launch", "navigation.launch.py"])),
        launch_arguments={
            "use_sim_time": "false",
            "pointcloud_topic": LaunchConfiguration("pointcloud_topic"),
            "map": LaunchConfiguration("map"),
            "slam": LaunchConfiguration("slam"),
            "nav2": LaunchConfiguration("nav2"),
        }.items(),
    )
    control = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(PathJoinSubstitution([pkg, "launch", "control.launch.py"])),
        launch_arguments={
            "use_sim_time": "false",
            "base_output_topic": "/cmd_vel",
            "base_odometry_topic": "/b2/odom",
            "robot_id": LaunchConfiguration("robot_id"),
            "teleop_allowed_peer": LaunchConfiguration("teleop_allowed_peer"),
        }.items(),
    )
    station_bridge = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(PathJoinSubstitution([bridge, "launch", "bridge.launch.py"])),
        launch_arguments={
            "use_sim_time": "false",
            "robot_id": LaunchConfiguration("robot_id"),
            "robot_name": LaunchConfiguration("robot_name"),
            "maps_dir": LaunchConfiguration("maps_dir"),
            "robot_data_dir": LaunchConfiguration("robot_data_dir"),
            "initial_map": LaunchConfiguration("map"),
        }.items(),
        condition=IfCondition(LaunchConfiguration("bridge")),
    )
    rviz = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(PathJoinSubstitution([pkg, "launch", "rviz.launch.py"])),
        launch_arguments={
            "use_sim_time": "false",
            "profile": LaunchConfiguration("rviz_profile"),
        }.items(),
        condition=IfCondition(LaunchConfiguration("rviz")),
    )

    return LaunchDescription([
        DeclareLaunchArgument("domain_id", default_value="0"),
        DeclareLaunchArgument("robot_id", default_value=_robot_id_from_metadata()),
        DeclareLaunchArgument("teleop_allowed_peer", default_value=""),
        DeclareLaunchArgument("robot_name", default_value="1호기"),
        DeclareLaunchArgument("network_interface", default_value=""),
        DeclareLaunchArgument("pointcloud_topic", default_value="/b2/points"),
        DeclareLaunchArgument("lidar", default_value="xt32", choices=["none", "xt32"]),
        DeclareLaunchArgument(
            "xt32_config_file",
            default_value=PathJoinSubstitution([pandar_xt32, "config", "xt32.yaml"])),
        DeclareLaunchArgument("xt32_x", default_value="0.34218"),
        DeclareLaunchArgument("xt32_y", default_value="0.0"),
        DeclareLaunchArgument("xt32_z", default_value="0.20"),
        DeclareLaunchArgument("xt32_roll", default_value="0.0"),
        DeclareLaunchArgument("xt32_pitch", default_value="0.0"),
        DeclareLaunchArgument("xt32_yaw", default_value="0.0"),
        DeclareLaunchArgument("aurora", default_value="false"),
        DeclareLaunchArgument("aurora_ip", default_value="192.168.11.1"),
        DeclareLaunchArgument("vn100", default_value="false"),
        DeclareLaunchArgument(
            "vn100_config_file",
            default_value=PathJoinSubstitution([vectornav_vn100, "config", "vn100.yaml"])),
        DeclareLaunchArgument(
            "vn100_port",
            default_value="/dev/serial/by-id/usb-FTDI_USB-RS232-WE_AV0LFM92-if00-port0"),
        DeclareLaunchArgument("maps_dir", default_value="/var/lib/shalom/maps",
                              description="로봇이 소유하는 지도 번들 디렉터리"),
        DeclareLaunchArgument("robot_data_dir", default_value="/var/lib/shalom",
                              description="지도와 무관한 로봇 운용 데이터 디렉터리"),
        DeclareLaunchArgument("map", default_value="auto",
                              description="auto(기본 지도), none(SLAM), 절대 경로의 map.yaml"),
        OpaqueFunction(function=_resolve_default_map),
        DeclareLaunchArgument("slam", default_value="true"),
        DeclareLaunchArgument("nav2", default_value="true"),
        DeclareLaunchArgument("bridge", default_value="true"),
        DeclareLaunchArgument("rviz", default_value="false"),
        DeclareLaunchArgument("rviz_profile", default_value="slam_nav2",
                              choices=["slam", "nav2", "slam_nav2"]),
        platform,
        navigation,
        control,
        station_bridge,
        rviz,
    ])
