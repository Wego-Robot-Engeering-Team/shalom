"""Bring up the robot platform and its physical sensors.

This is deliberately below navigation and inspection.  It exposes the same
base, sensor, and TF interfaces for simulation and hardware, but starts no
mapping, localisation, planner, station bridge, or RViz process.
"""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription, SetEnvironmentVariable
from launch.conditions import LaunchConfigurationEquals
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.substitutions import FindPackageShare


POINTS_TOPIC = "/b2/points"


def generate_launch_description():
    pkg = FindPackageShare("robot_bringup")
    realsense_d455 = FindPackageShare("realsense_d455")
    velodyne = FindPackageShare("velodyne_vlp16")
    pandar_xt32 = FindPackageShare("pandar_xt32")
    slamtec_aurora = FindPackageShare("slamtec_aurora")
    sim_robot = IncludeLaunchDescription(
        PythonLaunchDescriptionSource([
            FindPackageShare("b2_mujoco"), "/launch/b2_sim.launch.py",
        ]),
        launch_arguments={
            "viewer": LaunchConfiguration("viewer"),
            "payload": LaunchConfiguration("payload"),
            # KISS-ICP owns odom -> base_link when navigation is started.
            "ground_truth_tf": "false",
        }.items(),
        condition=LaunchConfigurationEquals("robot", "sim"),
    )

    real_robot = IncludeLaunchDescription(
        PythonLaunchDescriptionSource([
            FindPackageShare("b2_base"), "/launch/b2_bringup.launch.py",
        ]),
        launch_arguments={"network_interface": LaunchConfiguration("network_interface")}.items(),
        condition=LaunchConfigurationEquals("robot", "real"),
    )

    d455_capture = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(
            PathJoinSubstitution([realsense_d455, "launch", "d455_capture.launch.py"])),
        launch_arguments={"serial": LaunchConfiguration("d455_serial")}.items(),
        condition=LaunchConfigurationEquals("d455", "true"),
    )

    aurora_driver = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(
            PathJoinSubstitution([slamtec_aurora, "launch", "aurora_s.launch.py"])),
        launch_arguments={"ip_address": LaunchConfiguration("aurora_ip")}.items(),
        condition=LaunchConfigurationEquals("aurora", "true"),
    )

    vlp16 = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(
            PathJoinSubstitution([velodyne, "launch", "vlp16.launch.py"])),
        launch_arguments={"points_topic": LaunchConfiguration("pointcloud_topic")}.items(),
        condition=LaunchConfigurationEquals("lidar", "vlp16"),
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

    return LaunchDescription([
        DeclareLaunchArgument("domain_id", default_value="0"),
        SetEnvironmentVariable("ROS_DOMAIN_ID", LaunchConfiguration("domain_id")),
        SetEnvironmentVariable("RMW_IMPLEMENTATION", "rmw_cyclonedds_cpp"),
        SetEnvironmentVariable(
            "CYCLONEDDS_URI",
            ["file://", PathJoinSubstitution([pkg, "config", "cyclonedds.xml"])]),
        DeclareLaunchArgument("robot", default_value="sim", choices=["sim", "real"],
                              description="sim | real"),
        DeclareLaunchArgument("use_sim_time", default_value="true",
                              description="Set false for robot:=real."),
        DeclareLaunchArgument("network_interface", default_value="",
                              description="B2 Ethernet interface for robot:=real."),
        DeclareLaunchArgument("pointcloud_topic", default_value=POINTS_TOPIC),
        DeclareLaunchArgument("lidar", default_value="none", choices=["none", "vlp16", "xt32"],
                              description="none | vlp16 (temporary) | xt32 (Hesai Pandar XT32)"),
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
        DeclareLaunchArgument("d455", default_value="true",
                              description="Run the D455 for still-image capture; no live HMI video is sent."),
        DeclareLaunchArgument("d455_serial", default_value=""),
        DeclareLaunchArgument("aurora", default_value="false"),
        DeclareLaunchArgument("aurora_ip", default_value="192.168.11.1"),
        DeclareLaunchArgument("viewer", default_value="true",
                              description="MuJoCo viewer; ignored on hardware."),
        DeclareLaunchArgument("payload", default_value="none", choices=["none", "fr3"],
                              description="FR3 payload model; simulator only."),
        sim_robot, real_robot, d455_capture, aurora_driver, vlp16, xt32,
    ])
