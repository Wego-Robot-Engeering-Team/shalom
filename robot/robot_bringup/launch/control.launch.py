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
    mission_manager_config = PathJoinSubstitution([
        FindPackageShare("robot_bringup"), "config", "mission_manager.yaml"
    ])
    use_sim_time = ParameterValue(LaunchConfiguration("use_sim_time"), value_type=bool)

    return LaunchDescription([
        DeclareLaunchArgument("use_sim_time", default_value="false"),
        DeclareLaunchArgument("mission_manager_config", default_value=mission_manager_config),
        DeclareLaunchArgument("base_output_topic", default_value="/cmd_vel"),
        DeclareLaunchArgument("base_odometry_topic", default_value="/b2/odom"),
        DeclareLaunchArgument("require_external_heartbeat", default_value="true"),
        DeclareLaunchArgument("teleop_udp_port", default_value="9090"),
        # Empty is deliberately fail-closed: only the commissioned HMI address
        # may inject UDP velocity packets on a physical robot.
        DeclareLaunchArgument("teleop_allowed_peer", default_value=""),
        DeclareLaunchArgument("robot_id", default_value="R1"),
        Node(
            package="mission_manager", executable="mission_manager_node",
            name="mission_manager", output="screen",
            parameters=[
                LaunchConfiguration("mission_manager_config"),
                {"use_sim_time": use_sim_time},
            ],
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
            parameters=[{"use_sim_time": use_sim_time}],
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
        Node(
            package="teleop_bridge", executable="teleop_bridge_node",
            name="teleop_bridge", output="screen",
            parameters=[{
                "use_sim_time": use_sim_time,
                "udp_port": ParameterValue(
                    LaunchConfiguration("teleop_udp_port"), value_type=int),
                "allowed_peer": LaunchConfiguration("teleop_allowed_peer"),
                "robot_id": LaunchConfiguration("robot_id"),
                "output_topic": "/motion/teleop/cmd_vel",
            }],
        ),
        Node(
            package="twist_mux", executable="twist_mux",
            name="twist_mux", output="screen",
            parameters=[twist_mux_config, {"use_sim_time": use_sim_time}],
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
            }],
        ),
    ])
