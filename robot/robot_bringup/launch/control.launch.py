# Copyright (c) 2026 WeGo Robotics. All rights reserved.

"""Bring up the sole base-command safety path.

Every command source enters twist_mux, then safety_gate is the only publisher
to the B2 driver's `/cmd_vel`. E-Stop therefore blocks teleop and Nav2 alike.
"""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    twist_mux_config = PathJoinSubstitution([
        FindPackageShare("robot_bringup"), "config", "twist_mux.yaml"
    ])

    return LaunchDescription([
        DeclareLaunchArgument("base_output_topic", default_value="/cmd_vel"),
        DeclareLaunchArgument("require_external_heartbeat", default_value="true"),
        Node(
            package="mission_manager", executable="mission_manager_node",
            name="mission_manager", output="screen",
        ),
        Node(
            package="safety_manager", executable="safety_manager_node",
            name="safety_manager", output="screen",
            parameters=[{
                "require_external_heartbeat": ParameterValue(
                    LaunchConfiguration("require_external_heartbeat"), value_type=bool),
            }],
        ),
        Node(
            package="motion_interlock_manager", executable="motion_interlock_manager_node",
            name="motion_interlock_manager", output="screen",
        ),
        Node(
            package="teleop_bridge", executable="teleop_bridge_node",
            name="teleop_bridge", output="screen",
        ),
        Node(
            package="twist_mux", executable="twist_mux",
            name="twist_mux", output="screen",
            parameters=[twist_mux_config],
            remappings=[("cmd_vel_out", "/motion/base/cmd_vel")],
        ),
        Node(
            package="safety_gate", executable="safety_gate_node",
            name="safety_gate", output="screen",
            parameters=[{"output_base_topic": LaunchConfiguration("base_output_topic")}],
        ),
    ])
