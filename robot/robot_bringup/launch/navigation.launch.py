# Copyright (c) 2026 WeGo Robotics. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Wego-Proprietary

"""Bring up the navigation stack for either a physical robot or a simulator."""

from launch import LaunchDescription
from launch.logging import get_logger
from pathlib import Path

from launch.actions import (
    DeclareLaunchArgument,
    GroupAction,
    IncludeLaunchDescription,
    OpaqueFunction,
    RegisterEventHandler,
    SetLaunchConfiguration,
)
from launch.conditions import IfCondition
from launch.event_handlers import OnShutdown
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution, PythonExpression
from launch_ros.actions import Node, SetRemap
from launch_ros.parameter_descriptions import ParameterValue
from launch_ros.substitutions import FindPackageShare


BASE_FRAME = "base_link"


def _prepare_docking(context, *_args, **_kwargs):
    """Derive the dock database without adding control settings to map files."""
    import tempfile

    from ament_index_python.packages import get_package_share_directory
    from shalom_docking.config import generate_nav2_config

    runtime = tempfile.TemporaryDirectory(prefix="shalom_docking_")
    try:
        source = Path(get_package_share_directory("robot_bringup")) / "navigation/config/nav2.yaml"
        params = generate_nav2_config(
            source, LaunchConfiguration("map").perform(context), runtime.name,
        )
    except Exception:
        runtime.cleanup()
        raise

    def cleanup(_event, _context):
        runtime.cleanup()
        return []

    get_logger("robot_bringup.navigation").info(f"생성된 도킹 DB 설정: {params}")
    return [
        SetLaunchConfiguration("nav2_params", str(params)),
        RegisterEventHandler(OnShutdown(on_shutdown=cleanup)),
    ]


def _resolve_map(context, *_args, **_kwargs):
    """Validate an explicitly selected map path before nodes start."""
    raw = LaunchConfiguration("map").perform(context).strip()
    if not raw:
        get_logger("robot_bringup.navigation").warning(
            "저장된 지도 경로가 없습니다. 지도 없이 SLAM으로 시작합니다.")
        return [SetLaunchConfiguration("map", "")]

    resolved = Path(raw).expanduser()
    if not resolved.is_absolute():
        raise RuntimeError(
            "map에는 절대 경로의 map.yaml을 지정해야 합니다. "
            "예: map:=/var/lib/shalom/maps/inspection_a/map.yaml "
            "(지도 없이 시작하려면 map 인자를 생략)"
        )

    if not resolved.is_file():
        raise RuntimeError(f"그런 지도가 없다: {resolved}")
    if resolved.name != "map.yaml":
        raise RuntimeError(f"지도 경로는 map.yaml이어야 합니다: {resolved}")
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
                    "params_file": LaunchConfiguration("nav2_params"),
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
    )
    amcl = Node(
        package="nav2_amcl",
        executable="amcl",
        name="amcl",
        output="screen",
        parameters=[PathJoinSubstitution([config, "amcl.yaml"]), {"use_sim_time": use_sim_time}],
    )
    localisation_manager = Node(
        package="nav2_lifecycle_manager",
        executable="lifecycle_manager",
        name="lifecycle_manager_localization",
        output="screen",
        parameters=[{"use_sim_time": use_sim_time,
                     "autostart": ParameterValue(localising, value_type=bool),
                     "node_names": ["map_server", "amcl"]}],
    )

    return LaunchDescription([
        DeclareLaunchArgument("use_sim_time", default_value="false"),
        DeclareLaunchArgument("pointcloud_topic", default_value="/b2/points"),
        DeclareLaunchArgument("map", default_value="",
                              description="절대 경로의 map.yaml 또는 빈 값"),
        DeclareLaunchArgument("slam", default_value="true"),
        DeclareLaunchArgument("nav2", default_value="true"),
        OpaqueFunction(function=_resolve_map),
        OpaqueFunction(function=_prepare_docking, condition=IfCondition(LaunchConfiguration("nav2"))),
        perception, odometry, map_server, amcl, localisation_manager, nav2,
    ])
