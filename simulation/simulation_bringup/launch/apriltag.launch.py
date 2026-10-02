# Copyright (c) 2026 WeGo Robotics. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Wego-Proprietary

"""Detect the MuJoCo test tag using an already running B2 front camera.

The camera produces ideal, undistorted images and a matching CameraInfo, so
image_raw can be connected directly to the detector's image_rect input.
This launch does not start another simulator or connect docking control.
"""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    detector = Node(
        package="apriltag_ros",
        executable="apriltag_node",
        name="apriltag_detector",
        namespace="b2/front_camera",
        output="screen",
        parameters=[
            LaunchConfiguration("params_file"),
            {"use_sim_time": ParameterValue(
                LaunchConfiguration("use_sim_time"), value_type=bool)},
        ],
        remappings=[
            ("image_rect", LaunchConfiguration("image_topic")),
            ("camera_info", LaunchConfiguration("camera_info_topic")),
            ("detections", "tag_detections"),
        ],
    )
    return LaunchDescription([
        DeclareLaunchArgument("use_sim_time", default_value="true"),
        DeclareLaunchArgument(
            "image_topic", default_value="/b2/front_camera/image_raw"),
        DeclareLaunchArgument(
            "camera_info_topic", default_value="/b2/front_camera/camera_info"),
        DeclareLaunchArgument(
            "params_file",
            default_value=PathJoinSubstitution([
                FindPackageShare("simulation_bringup"),
                "config", "apriltag_sim.yaml",
            ]),
            description="Simulation tag family, physical boundary size and detector settings.",
        ),
        detector,
    ])
