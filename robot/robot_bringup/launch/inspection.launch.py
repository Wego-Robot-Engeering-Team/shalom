"""Run the complete inspection stack.

The mission-manager C++ core exists under robot/control, but its ROS adapters and
safety-manager are not implemented yet. This launch therefore composes the
platform, navigation, HMI bridge, and optional RViz only.
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

    navigation = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(PathJoinSubstitution([pkg, "launch", "navigation.launch.py"])),
        launch_arguments={
            name: LaunchConfiguration(name)
            for name in ("domain_id", "robot", "use_sim_time", "network_interface",
                         "pointcloud_topic", "lidar", "cameras", "arm_camera_serial",
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
        DeclareLaunchArgument("robot", default_value="sim", choices=["sim", "real", "none"]),
        DeclareLaunchArgument("use_sim_time", default_value="true"),
        DeclareLaunchArgument("network_interface", default_value=""),
        DeclareLaunchArgument("pointcloud_topic", default_value="/b2/points"),
        DeclareLaunchArgument("lidar", default_value="none", choices=["none", "vlp16"]),
        DeclareLaunchArgument("cameras", default_value="true"),
        DeclareLaunchArgument("arm_camera_serial", default_value=""),
        DeclareLaunchArgument("aurora", default_value="false"),
        DeclareLaunchArgument("aurora_ip", default_value="192.168.11.1"),
        DeclareLaunchArgument("viewer", default_value="true"),
        DeclareLaunchArgument("payload", default_value="none", choices=["none", "fr3"]),
        DeclareLaunchArgument("map", default_value="latest"),
        DeclareLaunchArgument("slam", default_value="true"),
        DeclareLaunchArgument("nav2", default_value="true"),
        DeclareLaunchArgument("bridge", default_value="true"),
        DeclareLaunchArgument("rviz", default_value="true"),
        DeclareLaunchArgument("rviz_profile", default_value="slam_nav2",
                              choices=["slam", "nav2", "slam_nav2"]),
        navigation, station_bridge, rviz,
    ])
