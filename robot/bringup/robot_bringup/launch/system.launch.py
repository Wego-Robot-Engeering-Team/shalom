# Copyright (c) 2026 WeGo Robotics. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Wego-Proprietary

"""L3: mission, safety and motion-authority supervision."""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    use_sim_time = ParameterValue(LaunchConfiguration("use_sim_time"), value_type=bool)
    mission_config = PathJoinSubstitution([
        FindPackageShare("robot_bringup"), "config", "mission_manager.yaml",
    ])
    return LaunchDescription([
        DeclareLaunchArgument("use_sim_time", default_value="false"),
        DeclareLaunchArgument("mission_manager_config", default_value=mission_config),
        DeclareLaunchArgument("base_output_topic", default_value="/cmd_vel"),
        DeclareLaunchArgument("base_odometry_topic", default_value="/b2/odom"),
        DeclareLaunchArgument("require_external_heartbeat", default_value="true"),
        Node(
            package="mission_manager", executable="mission_manager_node",
            name="mission_manager", output="screen",
            parameters=[LaunchConfiguration("mission_manager_config"),
                        {"use_sim_time": use_sim_time}],
        ),
        Node(
            package="safety_manager", executable="safety_manager_node",
            name="safety_manager", output="screen",
            parameters=[{
                "use_sim_time": use_sim_time,
                "require_external_heartbeat": ParameterValue(
                    LaunchConfiguration("require_external_heartbeat"), value_type=bool),
            }],
        ),
        Node(
            package="motion_interlock_manager", executable="motion_interlock_manager_node",
            name="motion_interlock_manager", output="screen",
            parameters=[{"use_sim_time": use_sim_time, "state_publish_period_ms": 200}],
        ),
        Node(
            package="motion_interlock_manager", executable="base_motion_monitor_node",
            name="base_motion_monitor", output="screen",
            parameters=[{
                "use_sim_time": use_sim_time,
                "command_topic": LaunchConfiguration("base_output_topic"),
                "odometry_topic": LaunchConfiguration("base_odometry_topic"),
            }],
        ),
    ])
