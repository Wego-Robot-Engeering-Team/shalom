# Copyright (c) 2026 WeGo Robotics. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Wego-Proprietary

"""L1: start the physical B2 and commissioned production sensors."""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription, SetEnvironmentVariable
from launch.conditions import LaunchConfigurationEquals
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.substitutions import FindPackageShare


POINTS_TOPIC = "/b2/points"


def generate_launch_description():
    pkg = FindPackageShare("robot_bringup")
    pandar_xt32 = FindPackageShare("pandar_xt32")
    slamtec_aurora = FindPackageShare("slamtec_aurora")
    vectornav_vn100 = FindPackageShare("vectornav_vn100")

    real_robot = IncludeLaunchDescription(
        PythonLaunchDescriptionSource([
            FindPackageShare("b2_base"), "/launch/b2_bringup.launch.py",
        ]),
        launch_arguments={"network_interface": LaunchConfiguration("network_interface")}.items(),
    )

    aurora_driver = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(
            PathJoinSubstitution([slamtec_aurora, "launch", "aurora_s.launch.py"])),
        launch_arguments={"ip_address": LaunchConfiguration("aurora_ip")}.items(),
        condition=LaunchConfigurationEquals("aurora", "true"),
    )

    xt32 = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(
            PathJoinSubstitution([pandar_xt32, "launch", "xt32.launch.py"])),
        launch_arguments={
            "points_topic": LaunchConfiguration("pointcloud_topic"),
            "config_file": LaunchConfiguration("xt32_config_file"),
            "x": LaunchConfiguration("xt32_x"),
            "y": LaunchConfiguration("xt32_y"),
            "z": LaunchConfiguration("xt32_z"),
            "roll": LaunchConfiguration("xt32_roll"),
            "pitch": LaunchConfiguration("xt32_pitch"),
            "yaw": LaunchConfiguration("xt32_yaw"),
        }.items(),
        condition=LaunchConfigurationEquals("lidar", "xt32"),
    )

    vn100_driver = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(
            PathJoinSubstitution([vectornav_vn100, "launch", "vn100.launch.py"])),
        launch_arguments={
            "config_file": LaunchConfiguration("vn100_config_file"),
            "port": LaunchConfiguration("vn100_port"),
        }.items(),
        condition=LaunchConfigurationEquals("vn100", "true"),
    )

    return LaunchDescription([
        DeclareLaunchArgument("domain_id", default_value="0"),
        SetEnvironmentVariable("ROS_DOMAIN_ID", LaunchConfiguration("domain_id")),
        SetEnvironmentVariable("RMW_IMPLEMENTATION", "rmw_cyclonedds_cpp"),
        SetEnvironmentVariable(
            "CYCLONEDDS_URI",
            ["file://", PathJoinSubstitution([pkg, "config", "cyclonedds.xml"])]),
        DeclareLaunchArgument("network_interface", default_value="",
                              description="B2 Ethernet interface."),
        DeclareLaunchArgument("pointcloud_topic", default_value=POINTS_TOPIC),
        DeclareLaunchArgument("lidar", default_value="xt32", choices=["none", "xt32"],
                              description="none | xt32 (Hesai Pandar XT32)"),
        DeclareLaunchArgument(
            "xt32_config_file",
            default_value=PathJoinSubstitution([pandar_xt32, "config", "xt32.yaml"]),
            description="Commissioned XT32 YAML; the package template is only a starting point."),
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
        real_robot, aurora_driver, xt32, vn100_driver,
    ])
