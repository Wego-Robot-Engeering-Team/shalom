# Copyright (c) 2026 WeGo Robotics. All rights reserved.

"""Run the physical robot stack; it never starts MuJoCo or test sensors."""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription
from launch.conditions import IfCondition
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    pkg = FindPackageShare("robot_bringup")
    bridge = FindPackageShare("hmi_bridge")
    pandar_xt32 = FindPackageShare("pandar_xt32")

    platform = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(PathJoinSubstitution([pkg, "launch", "platform.launch.py"])),
        launch_arguments={
            name: LaunchConfiguration(name)
            for name in ("domain_id", "network_interface", "pointcloud_topic", "lidar",
                         "xt32_config_file", "xt32_x", "xt32_y", "xt32_z", "xt32_roll",
                         "xt32_pitch", "xt32_yaw", "aurora", "aurora_ip")
        }.items(),
    )
    navigation = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(PathJoinSubstitution([pkg, "launch", "navigation.launch.py"])),
        launch_arguments={
            "use_sim_time": "false",
            "pointcloud_topic": LaunchConfiguration("pointcloud_topic"),
            "maps_dir": LaunchConfiguration("maps_dir"),
            "map": LaunchConfiguration("map"),
            "slam": LaunchConfiguration("slam"),
            "nav2": LaunchConfiguration("nav2"),
        }.items(),
    )
    control = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(PathJoinSubstitution([pkg, "launch", "control.launch.py"])),
        launch_arguments={"base_output_topic": "/cmd_vel"}.items(),
    )
    station_bridge = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(PathJoinSubstitution([bridge, "launch", "bridge.launch.py"])),
        launch_arguments={
            "use_sim_time": "false",
            "robot_id": LaunchConfiguration("robot_id"),
            "robot_name": LaunchConfiguration("robot_name"),
            "maps_dir": LaunchConfiguration("maps_dir"),
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
        DeclareLaunchArgument("robot_id", default_value="R1"),
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
        DeclareLaunchArgument("maps_dir", default_value="/var/lib/shalom/maps",
                              description="로봇이 소유하는 지도 번들 디렉터리"),
        DeclareLaunchArgument("map", default_value="latest",
                              description="latest | <map_id> | <absolute yaml> | none"),
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
