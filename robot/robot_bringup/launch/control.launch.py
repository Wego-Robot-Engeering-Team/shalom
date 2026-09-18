# Copyright (c) 2026 WeGo Robotics. All rights reserved.

"""Bring up the sole base-command safety path.

Every command source enters motion_mux, then safety_gate is the only publisher
to the B2 driver's `/cmd_vel`. E-Stop therefore blocks teleop and Nav2 alike.
"""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue


def generate_launch_description():
    return LaunchDescription([
        DeclareLaunchArgument("base_output_topic", default_value="/cmd_vel"),
        DeclareLaunchArgument("require_external_heartbeat", default_value="false"),
        DeclareLaunchArgument("teleop_udp_port", default_value="9090"),
        # Empty is deliberately fail-closed: only the commissioned HMI address
        # may inject UDP velocity packets on a physical robot.
        DeclareLaunchArgument("teleop_allowed_peer", default_value=""),
        DeclareLaunchArgument("robot_id", default_value="R1"),
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
            parameters=[{
                "udp_port": ParameterValue(
                    LaunchConfiguration("teleop_udp_port"), value_type=int),
                "allowed_peer": LaunchConfiguration("teleop_allowed_peer"),
                "robot_id": LaunchConfiguration("robot_id"),
            }],
        ),
        Node(
            package="motion_mux", executable="motion_mux_node",
            name="motion_mux", output="screen",
            parameters=[{"output_topic": "/motion/base/cmd_vel"}],
        ),
        Node(
            package="joint_mux", executable="joint_mux_node",
            name="joint_mux", output="screen",
        ),
        Node(
            package="safety_gate", executable="safety_gate_node",
            name="safety_gate", output="screen",
            parameters=[{"output_base_topic": LaunchConfiguration("base_output_topic")}],
        ),
    ])
