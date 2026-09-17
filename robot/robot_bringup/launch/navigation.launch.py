# Copyright (c) 2026 WeGo Robotics. All rights reserved.

"""Bring up mapping, localisation, and Nav2 on top of the robot platform."""

from launch import LaunchDescription
import os
from pathlib import Path

from ament_index_python.packages import get_package_share_directory
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription, OpaqueFunction, SetLaunchConfiguration
from launch.conditions import IfCondition
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution, PythonExpression
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


BASE_FRAME = "base_link"


def _resolve_map(context, *_args, **_kwargs):
    """Resolve latest, a map name, or an absolute map path before nodes start."""
    raw = LaunchConfiguration("map").perform(context).strip()
    if raw in ("", "none", "slam"):
        return [SetLaunchConfiguration("map", "")]

    maps_dir = Path(get_package_share_directory("robot_bringup")) / "navigation" / "maps"
    if raw == "latest":
        found = sorted(maps_dir.glob("*.yaml"))
        if not found:
            raise RuntimeError(f"{maps_dir} 에 지도가 없다. map:=none 으로 SLAM을 사용하십시오.")
        resolved = found[-1]
    elif os.path.isabs(raw):
        resolved = Path(raw)
    else:
        resolved = maps_dir / (raw if raw.endswith(".yaml") else raw + ".yaml")

    if not resolved.is_file():
        raise RuntimeError(f"그런 지도가 없다: {resolved}")
    return [SetLaunchConfiguration("map", str(resolved))]


def generate_launch_description():
    pkg = FindPackageShare("robot_bringup")
    pandar_xt32 = FindPackageShare("pandar_xt32")
    config = PathJoinSubstitution([pkg, "navigation", "config"])
    lidar_slam = FindPackageShare("lidar_slam")
    kiss_icp = FindPackageShare("kiss_icp")
    nav2_bringup = FindPackageShare("nav2_bringup")
    use_sim_time = LaunchConfiguration("use_sim_time")
    localising = PythonExpression(["'", LaunchConfiguration("map"), "' != ''"])

    platform = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(PathJoinSubstitution([pkg, "launch", "robot.launch.py"])),
        launch_arguments={
            name: LaunchConfiguration(name)
            for name in ("domain_id", "robot", "use_sim_time", "network_interface",
                         "pointcloud_topic", "lidar", "xt32_config_file", "xt32_x", "xt32_y", "xt32_z",
                         "xt32_roll", "xt32_pitch", "xt32_yaw", "d455", "d455_serial",
                         "aurora", "aurora_ip", "viewer", "payload")
        }.items(),
    )

    perception = IncludeLaunchDescription(
        PythonLaunchDescriptionSource([lidar_slam, "/launch/ground_slam.launch.py"]),
        launch_arguments={
            "pointcloud_topic": LaunchConfiguration("pointcloud_topic"),
            "base_frame": BASE_FRAME,
            "use_sim_time": use_sim_time,
            "slam": PythonExpression([
                "'false' if '", LaunchConfiguration("map"), "' else '",
                LaunchConfiguration("slam"), "'",
            ]),
            "gseg_params_file": PathJoinSubstitution([config, "ground_segmentation.yaml"]),
            "ground_filter_params_file": PathJoinSubstitution([config, "ground_filter.yaml"]),
            "slam_params_file": PathJoinSubstitution([config, "slam_toolbox.yaml"]),
        }.items(),
    )

    odometry = IncludeLaunchDescription(
        PythonLaunchDescriptionSource([kiss_icp, "/launch/odometry.launch.py"]),
        launch_arguments={
            "topic": LaunchConfiguration("pointcloud_topic"),
            "base_frame": BASE_FRAME,
            "lidar_odom_frame": "odom",
            "invert_odom_tf": "False",
            "visualize": "False",
            "config_file": PathJoinSubstitution([config, "kiss_icp.yaml"]),
            "use_sim_time": use_sim_time,
        }.items(),
    )

    nav2 = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(PathJoinSubstitution([nav2_bringup, "launch", "bringup_launch.py"])),
        launch_arguments={
            "use_sim_time": use_sim_time,
            "slam": "False",
            "use_localization": "False",
            "autostart": "True",
            "use_composition": "False",
            "use_respawn": "False",
            "params_file": PathJoinSubstitution([config, "nav2.yaml"]),
        }.items(),
        condition=IfCondition(LaunchConfiguration("nav2")),
    )

    map_server = Node(
        package="nav2_map_server", executable="map_server", name="map_server", output="screen",
        parameters=[{"use_sim_time": use_sim_time, "yaml_filename": LaunchConfiguration("map")}],
        condition=IfCondition(localising),
    )
    amcl = Node(
        package="nav2_amcl", executable="amcl", name="amcl", output="screen",
        parameters=[PathJoinSubstitution([config, "amcl.yaml"]), {"use_sim_time": use_sim_time}],
        condition=IfCondition(localising),
    )
    localisation_manager = Node(
        package="nav2_lifecycle_manager", executable="lifecycle_manager",
        name="lifecycle_manager_localization", output="screen",
        parameters=[{"use_sim_time": use_sim_time, "autostart": True,
                     "node_names": ["map_server", "amcl"]}],
        condition=IfCondition(localising),
    )

    return LaunchDescription([
        DeclareLaunchArgument("domain_id", default_value="0"),
        DeclareLaunchArgument("robot", default_value="sim", choices=["sim", "real"]),
        DeclareLaunchArgument("use_sim_time", default_value="true"),
        DeclareLaunchArgument("network_interface", default_value=""),
        DeclareLaunchArgument("pointcloud_topic", default_value="/b2/points"),
        DeclareLaunchArgument("lidar", default_value="none", choices=["none", "vlp16", "xt32"]),
        DeclareLaunchArgument(
            "xt32_config_file",
            default_value=PathJoinSubstitution([pandar_xt32, "config", "xt32.yaml"])),
        DeclareLaunchArgument("xt32_x", default_value="0.34218"),
        DeclareLaunchArgument("xt32_y", default_value="0.0"),
        DeclareLaunchArgument("xt32_z", default_value="0.20"),
        DeclareLaunchArgument("xt32_roll", default_value="0.0"),
        DeclareLaunchArgument("xt32_pitch", default_value="0.0"),
        DeclareLaunchArgument("xt32_yaw", default_value="0.0"),
        DeclareLaunchArgument("d455", default_value="true"),
        DeclareLaunchArgument("d455_serial", default_value=""),
        DeclareLaunchArgument("aurora", default_value="false"),
        DeclareLaunchArgument("aurora_ip", default_value="192.168.11.1"),
        DeclareLaunchArgument("viewer", default_value="true"),
        DeclareLaunchArgument("payload", default_value="none", choices=["none", "fr3"]),
        DeclareLaunchArgument("map", default_value="latest",
                              description="latest | <name> | <absolute yaml> | none"),
        DeclareLaunchArgument("slam", default_value="true"),
        DeclareLaunchArgument("nav2", default_value="true"),
        OpaqueFunction(function=_resolve_map),
        platform, perception, odometry, map_server, amcl, localisation_manager, nav2,
    ])
