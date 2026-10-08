# Copyright (c) 2026 WeGo Robotics. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Wego-Proprietary

"""Run the B2 simulation as a robot-shaped HMI endpoint."""

from pathlib import Path

from launch import LaunchDescription
from launch.actions import (DeclareLaunchArgument, GroupAction, IncludeLaunchDescription,
                            ResetLaunchConfigurations, SetEnvironmentVariable)
from launch.conditions import IfCondition
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration, OrSubstitution, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare
from ament_index_python.packages import get_package_share_directory


def _writable_maps_dir():
    """In a symlink install, use the source maps tree, not copied package files."""
    installed = Path(get_package_share_directory("simulation_bringup")) / "maps"
    readme = installed / "README.md"
    return str(readme.resolve().parent)


def _writable_robot_data_dir():
    return str(Path(_writable_maps_dir()).parent / "robot_data")


def generate_launch_description():
    sim = FindPackageShare("simulation_bringup")
    robot = FindPackageShare("robot_bringup")
    mujoco = FindPackageShare("b2_mujoco")

    platform = GroupAction(actions=[IncludeLaunchDescription(
        PythonLaunchDescriptionSource(PathJoinSubstitution([mujoco, "launch", "b2_sim.launch.py"])),
        launch_arguments={
            "viewer": LaunchConfiguration("viewer"),
            "scene_file": LaunchConfiguration("scene_file"),
            "ground_truth_tf": "false",
            "front_camera": OrSubstitution(
                LaunchConfiguration("front_camera"), LaunchConfiguration("apriltag")),
        }.items(),
    )], scoped=True)
    # Do not share generic child arguments such as params_file with Nav2 or
    # ground segmentation. The detector gets its own defaults and sim clock.
    apriltag = GroupAction(
        actions=[ResetLaunchConfigurations(), IncludeLaunchDescription(
            PythonLaunchDescriptionSource(
                PathJoinSubstitution([sim, "launch", "apriltag.launch.py"])),
            launch_arguments={"use_sim_time": "true"}.items(),
        )],
        scoped=True,
        condition=IfCondition(LaunchConfiguration("apriltag")),
    )
    # Navigation chooses the startup map before gateway is configured.
    navigation = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(PathJoinSubstitution([
            FindPackageShare("navigation"), "launch", "navigation.launch.py",
        ])),
        launch_arguments={
            "use_sim_time": "true",
            **{name: LaunchConfiguration(name) for name in (
                "pointcloud_topic", "maps_dir", "map", "slam", "nav2",
            )},
        }.items(),
    )
    system = GroupAction(actions=[IncludeLaunchDescription(
        PythonLaunchDescriptionSource(PathJoinSubstitution([robot, "launch", "system.launch.py"])),
        launch_arguments={
            "use_sim_time": "true",
            "base_output_topic": "/cmd_vel",
            "base_odometry_topic": "/b2/odom_gt",
            **{name: LaunchConfiguration(name) for name in (
                "mission_manager_config", "require_external_heartbeat",
            )},
        }.items(),
    )], scoped=True)
    control = GroupAction(actions=[IncludeLaunchDescription(
        PythonLaunchDescriptionSource(PathJoinSubstitution([robot, "launch", "control.launch.py"])),
        launch_arguments={
            "use_sim_time": "true",
            "base_output_topic": "/cmd_vel",
            "twist_mux_config": LaunchConfiguration("twist_mux_config"),
        }.items(),
    )], scoped=True)
    gateway = GroupAction(actions=[IncludeLaunchDescription(
        PythonLaunchDescriptionSource(PathJoinSubstitution([robot, "launch", "gateway.launch.py"])),
        launch_arguments={
            "use_sim_time": "true",
            "teleop_allowed_peer": "127.0.0.1",
            **{name: LaunchConfiguration(name) for name in (
                "robot_id", "robot_name", "maps_dir", "robot_data_dir", "map", "bridge",
                "bridge_config", "estop_port", "teleop_udp_port",
            )},
        }.items(),
    )], scoped=True)
    rviz = GroupAction(actions=[IncludeLaunchDescription(
        PythonLaunchDescriptionSource(PathJoinSubstitution([robot, "launch", "rviz.launch.py"])),
        launch_arguments={
            "use_sim_time": "true", "profile": LaunchConfiguration("rviz_profile"),
        }.items(),
    )], scoped=True, condition=IfCondition(LaunchConfiguration("rviz")))
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
        DeclareLaunchArgument("robot_data_dir",
                              default_value=_writable_robot_data_dir(),
                              description="Map-independent robot data directory"),
        DeclareLaunchArgument(
            "map",
            default_value="auto",
            description="auto(기본 지도), none(SLAM), 절대 경로의 map.yaml",
        ),
        DeclareLaunchArgument("slam", default_value="true"),
        DeclareLaunchArgument("nav2", default_value="true"),
        DeclareLaunchArgument("mission_manager_config", default_value=PathJoinSubstitution([
            robot, "config", "mission_manager.yaml",
        ])),
        DeclareLaunchArgument("twist_mux_config", default_value=PathJoinSubstitution([
            robot, "config", "twist_mux.yaml",
        ])),
        DeclareLaunchArgument("require_external_heartbeat", default_value="true"),
        DeclareLaunchArgument("bridge", default_value="true"),
        DeclareLaunchArgument("estop_port", default_value="9091"),
        DeclareLaunchArgument("teleop_udp_port", default_value="9090"),
        DeclareLaunchArgument("viewer", default_value="true"),
        DeclareLaunchArgument(
            "front_camera", default_value="false",
            description="Publish the simulated front RGB camera and CameraInfo.",
        ),
        DeclareLaunchArgument(
            "apriltag", default_value="false",
            description="Detect the front-camera test tag and enable the camera.",
        ),
        DeclareLaunchArgument("rviz", default_value="true"),
        DeclareLaunchArgument("rviz_profile", default_value="slam_nav2",
                              choices=["slam", "nav2", "slam_nav2"]),
        DeclareLaunchArgument(
            "bridge_config",
            default_value=PathJoinSubstitution([sim, "config", "bridge_sim.yaml"])),
        # Navigation validates the map before MuJoCo or its adapters start.
        navigation, system, control, gateway, rviz, platform, apriltag, posture_adapter,
    ])
