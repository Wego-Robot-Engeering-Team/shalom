# Copyright (c) 2026 WeGo Robotics. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Wego-Proprietary

"""L2: command arbitration and the final actuator safety gate.

Every command source enters twist_mux, then safety_gate is the only publisher
to the B2 driver's `/cmd_vel`. E-Stop therefore blocks teleop and Nav2 alike.
"""

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    # This must be resolved while the launch description is assembled.  If the
    # system dependency is absent, failing later while actions are visited can
    # leave an already-started platform process behind after a partial launch.
    get_package_share_directory("twist_mux")

    twist_mux_config = PathJoinSubstitution([
        FindPackageShare("robot_bringup"), "config", "twist_mux.yaml"
    ])
    use_sim_time = ParameterValue(LaunchConfiguration("use_sim_time"), value_type=bool)

    return LaunchDescription([
        DeclareLaunchArgument("use_sim_time", default_value="false"),
        DeclareLaunchArgument("twist_mux_config", default_value=twist_mux_config),
        DeclareLaunchArgument("base_output_topic", default_value="/cmd_vel"),
        Node(
            package="twist_mux", executable="twist_mux",
            name="twist_mux", output="screen",
            parameters=[LaunchConfiguration("twist_mux_config"), {"use_sim_time": use_sim_time}],
            remappings=[("cmd_vel_out", "/motion/base/cmd_vel")],
        ),
        Node(
            package="joint_mux", executable="joint_mux_node",
            name="joint_mux", output="screen",
            parameters=[{"use_sim_time": use_sim_time}],
        ),
        Node(
            package="safety_gate", executable="safety_gate_node",
            name="safety_gate", output="screen",
            parameters=[{
                "use_sim_time": use_sim_time,
                "output_base_topic": LaunchConfiguration("base_output_topic"),
                "authority_timeout_ms": 500,
            }],
        ),
    ])
