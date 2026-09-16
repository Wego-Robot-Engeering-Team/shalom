"""Run the complete inspection stack.

The mission-manager C++ core and supervisory-control nodes exist under
robot/control. This launch intentionally does not include them yet: current
Nav2 and HMI command producers still use legacy direct topics and must be
remapped through motion_mux and safety_gate as one validated change.
"""

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

    navigation = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(PathJoinSubstitution([pkg, "launch", "navigation.launch.py"])),
        launch_arguments={
            name: LaunchConfiguration(name)
            for name in ("domain_id", "robot", "use_sim_time", "network_interface",
                         "pointcloud_topic", "lidar", "xt32_config_file", "xt32_x", "xt32_y", "xt32_z",
                         "xt32_roll", "xt32_pitch", "xt32_yaw", "d455", "d455_serial",
                         "aurora", "aurora_ip", "viewer", "payload", "map", "slam", "nav2")
        }.items(),
    )
    station_bridge = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(PathJoinSubstitution([bridge, "launch", "bridge.launch.py"])),
        launch_arguments={
            "use_sim_time": LaunchConfiguration("use_sim_time"),
            "robot_id": LaunchConfiguration("robot_id"),
            "robot_name": LaunchConfiguration("robot_name"),
        }.items(),
        condition=IfCondition(LaunchConfiguration("bridge")),
    )
    rviz = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(PathJoinSubstitution([pkg, "launch", "rviz.launch.py"])),
        launch_arguments={
            "use_sim_time": LaunchConfiguration("use_sim_time"),
            "profile": LaunchConfiguration("rviz_profile"),
        }.items(),
        condition=IfCondition(LaunchConfiguration("rviz")),
    )

    return LaunchDescription([
        DeclareLaunchArgument("domain_id", default_value="0"),
        DeclareLaunchArgument("robot_id", default_value="R1"),
        DeclareLaunchArgument("robot_name", default_value="1호기"),
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
        DeclareLaunchArgument("bridge", default_value="true"),
        DeclareLaunchArgument("rviz", default_value="true"),
        DeclareLaunchArgument("rviz_profile", default_value="slam_nav2",
                              choices=["slam", "nav2", "slam_nav2"]),
        navigation, station_bridge, rviz,
    ])
