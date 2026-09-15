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
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


POINTS_TOPIC = "/b2/points"
BASE_FRAME = "base_link"


def generate_launch_description():
    pkg = FindPackageShare("robot_bringup")
    realsense = FindPackageShare("realsense_d455")
    velodyne = FindPackageShare("velodyne_vlp16")
    pandar_xt32 = FindPackageShare("pandar_xt32")
    slamtec_aurora = FindPackageShare("slamtec_aurora")
    use_sim_time = LaunchConfiguration("use_sim_time")

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

    # A stationary TF source for camera and bridge integration without a B2.
    bench_robot = Node(
        package="robot_bringup",
        executable="bench_odom",
        name="bench_odom",
        output="screen",
        parameters=[{"use_sim_time": use_sim_time, "base_frame": BASE_FRAME}],
        condition=LaunchConfigurationEquals("robot", "none"),
    )

    arm_camera = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(
            PathJoinSubstitution([realsense, "launch", "d455_stream.launch.py"])),
        launch_arguments={
            "role": "arm",
            "serial": LaunchConfiguration("arm_camera_serial"),
        }.items(),
        condition=LaunchConfigurationEquals("cameras", "true"),
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
        DeclareLaunchArgument("robot", default_value="sim", choices=["sim", "real", "none"],
                              description="sim | real | none (stationary integration fixture)"),
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
        DeclareLaunchArgument("cameras", default_value="true"),
        DeclareLaunchArgument("arm_camera_serial", default_value=""),
        DeclareLaunchArgument("aurora", default_value="false"),
        DeclareLaunchArgument("aurora_ip", default_value="192.168.11.1"),
        DeclareLaunchArgument("viewer", default_value="true",
                              description="MuJoCo viewer; ignored on hardware."),
        DeclareLaunchArgument("payload", default_value="none", choices=["none", "fr3"],
                              description="FR3 payload model; simulator only."),
        sim_robot, real_robot, bench_robot, arm_camera, aurora_driver, vlp16, xt32,
    ])
