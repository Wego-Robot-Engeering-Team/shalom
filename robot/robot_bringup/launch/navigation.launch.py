# Copyright (c) 2026 WeGo Robotics. All rights reserved.

"""Bring up the navigation stack for either a physical robot or a simulator."""

from launch import LaunchDescription
import os
from pathlib import Path

from ament_index_python.packages import get_package_share_directory
from launch.actions import (
    DeclareLaunchArgument,
    GroupAction,
    IncludeLaunchDescription,
    OpaqueFunction,
    SetLaunchConfiguration,
)
from launch.conditions import IfCondition
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution, PythonExpression
from launch_ros.actions import Node, SetRemap
from launch_ros.substitutions import FindPackageShare


BASE_FRAME = "base_link"


def _resolve_map(context, *_args, **_kwargs):
    """Resolve latest, a map name, or an absolute map path before nodes start."""
    raw = LaunchConfiguration("map").perform(context).strip()
    if raw in ("", "none", "slam"):
        return [SetLaunchConfiguration("map", "")]

    maps_dir = Path(LaunchConfiguration("maps_dir").perform(context)).expanduser()
    if raw == "latest":
        found = sorted(p for p in maps_dir.glob("*/map.yaml") if p.is_file())
        if not found:
            found = sorted(p for p in maps_dir.glob("*.yaml") if p.is_file())
        if not found:
            raise RuntimeError(f"{maps_dir} 에 지도가 없다. map:=none 으로 SLAM을 사용하십시오.")
        resolved = found[-1]
    elif os.path.isabs(raw):
        resolved = Path(raw)
    else:
        flat = maps_dir / (raw if raw.endswith(".yaml") else raw + ".yaml")
        packaged = maps_dir / raw / "map.yaml"
        resolved = flat if flat.is_file() else packaged

    if not resolved.is_file():
        raise RuntimeError(f"그런 지도가 없다: {resolved}")
    return [SetLaunchConfiguration("map", str(resolved))]


def generate_launch_description():
    pkg = FindPackageShare("robot_bringup")
    config = PathJoinSubstitution([pkg, "navigation", "config"])
    lidar_slam = FindPackageShare("lidar_slam")
    kiss_icp = FindPackageShare("kiss_icp")
    nav2_bringup = FindPackageShare("nav2_bringup")
    use_sim_time = LaunchConfiguration("use_sim_time")
    localising = PythonExpression(["'", LaunchConfiguration("map"), "' != ''"])

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

    nav2 = GroupAction(
        actions=[
            # opennav_docking publishes on its fixed relative `cmd_vel` topic.
            # Keep it inside the sole actuator path instead of allowing it to
            # bypass twist_mux and safety_gate.  The node-qualified rule is
            # inherited by Nav2's included launch but applies only to the
            # docking_server process.
            SetRemap(
                src="docking_server:cmd_vel",
                dst="/motion/dock/cmd_vel",
            ),
            IncludeLaunchDescription(
                PythonLaunchDescriptionSource(
                    PathJoinSubstitution([nav2_bringup, "launch", "bringup_launch.py"])
                ),
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
            ),
        ],
    )

    map_server = Node(
        package="nav2_map_server",
        executable="map_server",
        name="map_server",
        output="screen",
        parameters=[{"use_sim_time": use_sim_time, "yaml_filename": LaunchConfiguration("map")}],
        condition=IfCondition(localising),
    )
    amcl = Node(
        package="nav2_amcl",
        executable="amcl",
        name="amcl",
        output="screen",
        parameters=[PathJoinSubstitution([config, "amcl.yaml"]), {"use_sim_time": use_sim_time}],
        condition=IfCondition(localising),
    )
    localisation_manager = Node(
        package="nav2_lifecycle_manager",
        executable="lifecycle_manager",
        name="lifecycle_manager_localization",
        output="screen",
        parameters=[{"use_sim_time": use_sim_time, "autostart": True,
                     "node_names": ["map_server", "amcl"]}],
        condition=IfCondition(localising),
    )

    return LaunchDescription([
        DeclareLaunchArgument("use_sim_time", default_value="false"),
        DeclareLaunchArgument("pointcloud_topic", default_value="/b2/points"),
        DeclareLaunchArgument("maps_dir", default_value="/var/lib/shalom/maps",
                              description="지도 번들과 지도별 상태가 있는 디렉터리"),
        DeclareLaunchArgument("map", default_value="latest",
                              description="latest | <name> | <absolute yaml> | none"),
        DeclareLaunchArgument("slam", default_value="true"),
        DeclareLaunchArgument("nav2", default_value="true"),
        OpaqueFunction(function=_resolve_map),
        perception, odometry, map_server, amcl, localisation_manager, nav2,
    ])
