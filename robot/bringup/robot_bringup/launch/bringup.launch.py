# Copyright (c) 2026 WeGo Robotics. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Wego-Proprietary

"""Run the physical robot stack; it never starts MuJoCo or test sensors."""

from pathlib import Path

import yaml
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription, OpaqueFunction
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.substitutions import FindPackageShare
from robot_bringup.map_selection import resolve_default_map


def _robot_id_from_metadata():
    metadata_path = Path(get_package_share_directory("robot_bringup")) / "config" / "robot_metadata.yaml"
    with metadata_path.open(encoding="utf-8") as metadata_file:
        metadata = yaml.safe_load(metadata_file) or {}
    robot_id = metadata.get("robot", {}).get("id", "")
    if not robot_id:
        raise RuntimeError(f"robot id is missing from {metadata_path}")
    return str(robot_id)


def generate_launch_description():
    pkg = FindPackageShare("robot_bringup")
    pandar_xt32 = FindPackageShare("pandar_xt32")
    vectornav_vn100 = FindPackageShare("vectornav_vn100")

    drivers = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(PathJoinSubstitution([pkg, "launch", "drivers.launch.py"])),
        launch_arguments={
            name: LaunchConfiguration(name)
            for name in ("domain_id", "network_interface", "pointcloud_topic", "lidar",
                         "xt32_config_file", "xt32_x", "xt32_y", "xt32_z", "xt32_roll",
                         "xt32_pitch", "xt32_yaw", "aurora", "aurora_ip", "vn100",
                         "vn100_config_file", "vn100_port")
        }.items(),
    )
    runtime = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(PathJoinSubstitution([pkg, "launch", "runtime.launch.py"])),
        launch_arguments={
            "use_sim_time": "false",
            "pointcloud_topic": LaunchConfiguration("pointcloud_topic"),
            "base_output_topic": "/cmd_vel",
            "base_odometry_topic": "/b2/odom",
            **{name: LaunchConfiguration(name) for name in (
                "map", "slam", "nav2", "robot_id", "robot_name", "maps_dir",
                "robot_data_dir", "teleop_allowed_peer", "bridge", "rviz", "rviz_profile",
            )},
        }.items(),
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
        DeclareLaunchArgument("slam", default_value="true"),
        DeclareLaunchArgument("nav2", default_value="true"),
        DeclareLaunchArgument("bridge", default_value="true"),
        DeclareLaunchArgument("rviz", default_value="false"),
        DeclareLaunchArgument("rviz_profile", default_value="slam_nav2",
                              choices=["slam", "nav2", "slam_nav2"]),
        OpaqueFunction(function=resolve_default_map),
        drivers,
        runtime,
    ])
