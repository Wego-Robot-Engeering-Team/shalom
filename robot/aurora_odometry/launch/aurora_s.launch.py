"""SLAMTEC Aurora S의 원시 6DoF odometry를 안전하게 올린다.

기존 KISS-ICP의 odom -> base_link와 충돌하지 않도록 Aurora는
aurora_odom -> aurora_link 및 /aurora/odom만 발행한다.
"""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    config = PathJoinSubstitution(
        [FindPackageShare("aurora_odometry"), "config", "aurora_s.yaml"])

    return LaunchDescription([
        DeclareLaunchArgument(
            "ip_address", default_value="192.168.11.1",
            description="Aurora S 유선 관리/SDK 주소"),
        Node(
            package="slamware_ros_sdk",
            executable="slamware_ros_sdk_server_node",
            name="aurora_driver",
            parameters=[config, {"ip_address": LaunchConfiguration("ip_address")}],
            output="screen",
            respawn=True,
            respawn_delay=3.0,
        ),
    ])
