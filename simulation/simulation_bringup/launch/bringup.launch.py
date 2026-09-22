# Copyright (c) 2026 WeGo Robotics. All rights reserved.

"""Run the B2 simulation as a robot-shaped HMI endpoint."""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription, SetEnvironmentVariable
from launch.conditions import IfCondition
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    sim = FindPackageShare("simulation_bringup")
    robot = FindPackageShare("robot_bringup")
    bridge = FindPackageShare("hmi_bridge")
    mujoco = FindPackageShare("b2_mujoco")

    platform = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(PathJoinSubstitution([mujoco, "launch", "b2_sim.launch.py"])),
        launch_arguments={
            "viewer": LaunchConfiguration("viewer"),
            "ground_truth_tf": "false",
        }.items(),
    )
    navigation = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(PathJoinSubstitution([robot, "launch", "navigation.launch.py"])),
        launch_arguments={
            "use_sim_time": "true",
            "pointcloud_topic": LaunchConfiguration("pointcloud_topic"),
            "maps_dir": LaunchConfiguration("maps_dir"),
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
        DeclareLaunchArgument("pointcloud_topic", default_value="/b2/points"),
        DeclareLaunchArgument("maps_dir",
                              default_value=PathJoinSubstitution([sim, "maps"]),
                              description="Simulator-owned map bundle directory"),
        DeclareLaunchArgument("map", default_value="2026-09-07",
                              description="latest | <map_id> | <absolute yaml> | none"),
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
