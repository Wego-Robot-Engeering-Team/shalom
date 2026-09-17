# Copyright (c) 2026 WeGo Robotics. All rights reserved.

"""Launch one RViz profile against an already-running robot stack.

This launch intentionally starts no robot, sensor, SLAM, or Nav2 nodes.  It is
for observing the ROS graph that is already present, whether that graph comes
from simulation, hardware, or a separately started subsystem.
"""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution, PythonExpression
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    profile = DeclareLaunchArgument(
        "profile",
        default_value="slam_nav2",
        choices=["slam", "nav2", "slam_nav2"],
        description="RViz profile: slam | nav2 | slam_nav2",
    )
    use_sim_time = DeclareLaunchArgument(
        "use_sim_time",
        default_value="false",
        description="Set true only when the observed stack publishes /clock.",
    )

    config = PathJoinSubstitution([
        FindPackageShare("robot_bringup"),
        "navigation",
        "rviz",
        PythonExpression(["'", LaunchConfiguration("profile"), ".rviz'"]),
    ])

    rviz = Node(
        package="rviz2",
        executable="rviz2",
        name="rviz2",
        output="screen",
        arguments=["-d", config],
        parameters=[{"use_sim_time": LaunchConfiguration("use_sim_time")}],
    )

    return LaunchDescription([profile, use_sim_time, rviz])
